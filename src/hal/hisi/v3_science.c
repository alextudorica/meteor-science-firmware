// Science mode for Hi3516CV300 (hisi-gen3): see science.h.
//
// The per-frame record follows RMS alpha2 RMS/RMSPSei.py (RMSP v5, 61 bytes, fields
// XOR 0xFF after the magic, XOR checksum in the last byte), and the control commands
// follow Utils/CameraControl.py ispControl()/openipcCall() on the RMS side.

#ifdef SCIENCE

#include <dlfcn.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "v3_science.h"
#include "../../science.h"

extern float v3_system_readtemp(void);

#define SCI_CONF "/root/science/science.conf"
#define SCI_PORT 9600
#define ISP_DEV_ID 0
#define VPSS_GRP_ID 0

static v3_science_impl sdk;

typedef struct {
    double lat, lon, switchDeg;
    double monoFromH, monoToH;      // UTC hours of a black-and-white window inside the night
    int nightSatu, chromaQpOffset;
    double sensorFps;               // ISP frame rate; trims VMAX so frames come at 40.000 ms
    unsigned int nightAGain, nightDGain, nightIspDGain, nightExp;
    unsigned int dayMaxExp, dayMaxDGain;
    int daySatu;
    int dpcOn;                      // 1 = defect pixel correction active (not bypassed)
    int blackLevel;                 // 12-bit black level the ISP subtracts; -1 = leave the sensor default
    int pedestal;                   // 12-bit sensor black level removed in the gamma table (IMX291: 240)
} sci_conf;

static sci_conf conf = {
    .lat = NAN, .lon = NAN, .switchDeg = -5.0, .sensorFps = NAN,
    .monoFromH = NAN, .monoToH = NAN, .nightSatu = 0, .chromaQpOffset = 0,
    // Night baseline: starting values from an IMX307 profile; tune per camera and site.
    .nightAGain = 22924, .nightDGain = 1024, .nightIspDGain = 2048, .nightExp = 39941,
    .dayMaxExp = 39970, .dayMaxDGain = 1024, .daySatu = 128,
    .blackLevel = -1, .pedestal = 240,
};

static int enforceOn = 1;           // 0 = maintenance loop leaves module control alone (experiments)

static pthread_mutex_t ispMtx = PTHREAD_MUTEX_INITIALIZER;
static v3_isp_expinfo expInfo;      // large (1024-bin histogram): keep it off the stack
static int night = -1;              // -1 unknown, 0 day, 1 night
static int satuNow = -1;
static int lastSunNight = -1;
static float tempC = NAN;
static time_t tempAt;

static void sci_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[science] ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static void *sym(const char *name)
{
    void *p = dlsym(RTLD_DEFAULT, name);
    if (!p) sci_log("missing symbol %s\n", name);
    return p;
}

static void sci_resolve(void)
{
    sdk.fnGetModuleControl = sym("HI_MPI_ISP_GetModuleControl");
    sdk.fnSetModuleControl = sym("HI_MPI_ISP_SetModuleControl");
    sdk.fnGetGamma = sym("HI_MPI_ISP_GetGammaAttr");
    sdk.fnSetGamma = sym("HI_MPI_ISP_SetGammaAttr");
    sdk.fnGetExposure = sym("HI_MPI_ISP_GetExposureAttr");
    sdk.fnSetExposure = sym("HI_MPI_ISP_SetExposureAttr");
    sdk.fnQueryExposure = sym("HI_MPI_ISP_QueryExposureInfo");
    sdk.fnGetWB = sym("HI_MPI_ISP_GetWBAttr");
    sdk.fnSetWB = sym("HI_MPI_ISP_SetWBAttr");
    sdk.fnQueryWB = sym("HI_MPI_ISP_QueryWBInfo");
    sdk.fnGetSaturation = sym("HI_MPI_ISP_GetSaturationAttr");
    sdk.fnSetSaturation = sym("HI_MPI_ISP_SetSaturationAttr");
    sdk.fnGetGroupConfig = sym("HI_MPI_VPSS_GetGrpAttr");
    sdk.fnSetGroupConfig = sym("HI_MPI_VPSS_SetGrpAttr");
    sdk.fnGetH264Dblk = sym("HI_MPI_VENC_GetH264Dblk");
    sdk.fnSetH264Dblk = sym("HI_MPI_VENC_SetH264Dblk");
    sdk.fnGetCurrentPts = sym("HI_MPI_SYS_GetCurPts");
    sdk.fnGetDeviceConfig = sym("HI_MPI_ISP_GetPubAttr");
    sdk.fnGetH264Trans = sym("HI_MPI_VENC_GetH264Trans");
    sdk.fnSetH264Trans = sym("HI_MPI_VENC_SetH264Trans");
    sdk.fnSetDeviceConfig = sym("HI_MPI_ISP_SetPubAttr");
    sdk.fnGetBlackLevel = sym("HI_MPI_ISP_GetBlackLevelAttr");
    sdk.fnSetBlackLevel = sym("HI_MPI_ISP_SetBlackLevelAttr");
}

static int confLoaded;

static void sci_load_conf(void)
{
    if (confLoaded) return;
    confLoaded = 1;
    FILE *f = fopen(SCI_CONF, "r");
    if (!f) {
        sci_log("no %s, using defaults (no sun-driven day/night)\n", SCI_CONF);
        return;
    }
    char line[160], key[64];
    double v;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || sscanf(line, " %63[a-z_] = %lf", key, &v) != 2)
            continue;
        if (!strcmp(key, "lat")) conf.lat = v;
        else if (!strcmp(key, "lon")) conf.lon = v;
        else if (!strcmp(key, "switch_deg")) conf.switchDeg = v;
        else if (!strcmp(key, "night_again")) conf.nightAGain = (unsigned int)v;
        else if (!strcmp(key, "night_dgain")) conf.nightDGain = (unsigned int)v;
        else if (!strcmp(key, "night_ispdgain")) conf.nightIspDGain = (unsigned int)v;
        else if (!strcmp(key, "night_exp")) conf.nightExp = (unsigned int)v;
        else if (!strcmp(key, "day_max_exp")) conf.dayMaxExp = (unsigned int)v;
        else if (!strcmp(key, "day_max_dgain")) conf.dayMaxDGain = (unsigned int)v;
        else if (!strcmp(key, "day_satu")) conf.daySatu = (int)v;
        else if (!strcmp(key, "sensor_fps")) conf.sensorFps = v;
        else if (!strcmp(key, "night_satu")) conf.nightSatu = (int)v;
        else if (!strcmp(key, "dpc")) conf.dpcOn = (int)v;
        else if (!strcmp(key, "black_level")) conf.blackLevel = (int)v;
        else if (!strcmp(key, "pedestal")) conf.pedestal = (int)v;
        else if (!strcmp(key, "chroma_qp_offset")) conf.chromaQpOffset = (int)v;
        else if (!strcmp(key, "mono_from_utc")) conf.monoFromH = v;
        else if (!strcmp(key, "mono_to_utc")) conf.monoToH = v;
    }
    fclose(f);
    sci_log("conf lat=%.4f lon=%.4f switch=%.1f night a=%u d=%u i=%u e=%u satu=%d chroma_qp=%+d mono %.2f-%.2f\n",
        conf.lat, conf.lon, conf.switchDeg, conf.nightAGain, conf.nightDGain,
        conf.nightIspDGain, conf.nightExp, conf.nightSatu, conf.chromaQpOffset,
        conf.monoFromH, conf.monoToH);
}

// ---------------------------------------------------------------------------
// ISP and encoder controls (callers hold ispMtx)
// ---------------------------------------------------------------------------

// Science invariants: every module that alters the image beyond demosaic, WB, CCM
// and the fixed gamma is bypassed. Returns 1 if anything had to be changed.
static int sci_enforce_modules(void)
{
    if (!enforceOn || !sdk.fnGetModuleControl || !sdk.fnSetModuleControl) return 0;
    unsigned int m;
    if (sdk.fnGetModuleControl(ISP_DEV_ID, &m)) return 0;
    unsigned int before = m;
    m |= V3_ISP_BYPASS_NR | V3_ISP_BYPASS_SHARPEN | V3_ISP_BYPASS_DRC |
        V3_ISP_BYPASS_ANTIFC | V3_ISP_BYPASS_CROSSTALK | V3_ISP_BYPASS_DEHAZE |
        V3_ISP_BYPASS_CA | V3_ISP_BYPASS_CAC | V3_ISP_BYPASS_SHADING;
    if (conf.dpcOn) m &= ~V3_ISP_BYPASS_DPC;
    else m |= V3_ISP_BYPASS_DPC;
    m &= ~V3_ISP_BYPASS_GAMMA;
    if (m == before) return 0;
    int r = sdk.fnSetModuleControl(ISP_DEV_ID, &m);
    sci_log("module control %#x -> %#x (%#x)\n", before, m, r);
    return 1;
}

// Black level the ISP subtracts before WB, CCM and gamma. Without it the sensor pedestal
// (IMX291: 240 of 4095) passes the gamma 0.5 table and lands at ~62 of 255, where the
// curve is flat, so the night sky, its noise and faint stars share 1-2 output codes.
static int sci_enforce_blc(void)
{
    if (conf.blackLevel < 0 || !sdk.fnGetBlackLevel || !sdk.fnSetBlackLevel) return 0;
    v3_isp_black b;
    if (sdk.fnGetBlackLevel(ISP_DEV_ID, &b)) return 0;
    unsigned short want = (unsigned short)(conf.blackLevel > 4095 ? 4095 : conf.blackLevel);
    if (b.level[0] == want && b.level[1] == want &&
        b.level[2] == want && b.level[3] == want)
        return 0;
    sci_log("black level was %u %u %u %u\n", b.level[0], b.level[1],
        b.level[2], b.level[3]);
    for (int i = 0; i < 4; i++) b.level[i] = want;
    int r = sdk.fnSetBlackLevel(ISP_DEV_ID, &b);
    sci_log("black level %u set (%#x)\n", want, r);
    return 1;
}

// Gamma 0.5 measured from the sensor black level: out = 4095 * sqrt((in - P) / (4095 - P)),
// 0 at and below P. The CV300 path to the gamma table never subtracts the IMX291 pedestal
// (HI_MPI_ISP_SetBlackLevelAttr has no effect on the output here), so a table that starts at
// 0 put black at 62/255, where the curve is flat: the night sky, its noise and stars of mag 4-5
// shared 1-2 output codes. Nodes are 16 input codes apart; the first segment above P is linear.
static unsigned short sciGammaTable[V3_ISP_GAMMA_NODES];
static int sciGammaPedestal = -1;

static void sci_build_gamma(void)
{
    int ped = conf.pedestal < 0 ? 0 : conf.pedestal > 2048 ? 2048 : conf.pedestal;
    if (ped == sciGammaPedestal) return;
    for (int i = 0; i < V3_ISP_GAMMA_NODES; i++) {
        double in = i * 4096.0 / (V3_ISP_GAMMA_NODES - 1);
        double x = (in - ped) / (4096.0 - ped);
        sciGammaTable[i] = (unsigned short)lround(4095.0 * sqrt(x < 0 ? 0 : x > 1 ? 1 : x));
    }
    sciGammaTable[V3_ISP_GAMMA_NODES - 1] = 4095;
    sciGammaPedestal = ped;
}

static int sci_gamma_is_sqrt(const v3_isp_gamma *g)
{
    if (!g->enable || g->curve != V3_ISP_GAMMA_CUSTOM) return 0;
    for (int i = 0; i < V3_ISP_GAMMA_NODES; i++)
        if (g->table[i] != sciGammaTable[i]) return 0;
    return 1;
}

static int sci_enforce_gamma(void)
{
    if (!sdk.fnGetGamma || !sdk.fnSetGamma) return 0;
    static v3_isp_gamma g;
    sci_build_gamma();
    if (sdk.fnGetGamma(ISP_DEV_ID, &g)) return 0;
    if (sci_gamma_is_sqrt(&g)) return 0;
    sci_log("gamma was type %d en %d, table[0]=%u [16]=%u [128]=%u [256]=%u\n", g.curve,
        g.enable, g.table[0], g.table[16], g.table[128], g.table[V3_ISP_GAMMA_NODES - 1]);
    g.enable = 1;
    g.curve = V3_ISP_GAMMA_CUSTOM;
    memcpy(g.table, sciGammaTable, sizeof(sciGammaTable));
    int r = sdk.fnSetGamma(ISP_DEV_ID, &g);
    sci_log("gamma 0.5 above pedestal %d set (%#x)\n", sciGammaPedestal, r);
    return 1;
}

static int sci_enforce_vpss(void)
{
    if (!sdk.fnGetGroupConfig || !sdk.fnSetGroupConfig || getenv("SCI_VPSS_NR")) return 0;
    v3_vpss_grp a;
    if (sdk.fnGetGroupConfig(VPSS_GRP_ID, &a)) return 0;
    if (!a.nredOn && !a.enhOn && !a.dciOn && !a.sharpOn) return 0;
    a.nredOn = a.enhOn = a.dciOn = a.sharpOn = 0;
    int r = sdk.fnSetGroupConfig(VPSS_GRP_ID, &a);
    sci_log("VPSS NR/IE/DCI/sharpen off (%#x)\n", r);
    return 1;
}

static int sci_manual(unsigned int again, unsigned int dgain, unsigned int ispdgain, unsigned int exp)
{
    if (!sdk.fnGetExposure || !sdk.fnSetExposure) return -1;
    v3_isp_exp *e = calloc(1, sizeof(*e));
    if (!e) return -1;
    int r = sdk.fnGetExposure(ISP_DEV_ID, e);
    if (!r) {
        e->bypass = 0;
        e->op = V3_ISP_OP_MANUAL;
        e->manual.expTimeOp = V3_ISP_OP_MANUAL;
        e->manual.aGainOp = V3_ISP_OP_MANUAL;
        e->manual.dGainOp = V3_ISP_OP_MANUAL;
        e->manual.ispDGainOp = V3_ISP_OP_MANUAL;
        e->manual.aGain = again;
        e->manual.dGain = dgain;
        e->manual.ispDGain = ispdgain;
        e->manual.expTime = exp;
        r = sdk.fnSetExposure(ISP_DEV_ID, e);
    }
    free(e);
    sci_log("manual a=%u d=%u i=%u e=%u (%#x)\n", again, dgain, ispdgain, exp, r);
    return r;
}

static int sci_auto(unsigned int minExp, unsigned int maxExp, unsigned int maxDGain)
{
    if (!sdk.fnGetExposure || !sdk.fnSetExposure) return -1;
    v3_isp_exp *e = calloc(1, sizeof(*e));
    if (!e) return -1;
    int r = sdk.fnGetExposure(ISP_DEV_ID, e);
    if (!r) {
        e->bypass = 0;
        e->op = V3_ISP_OP_AUTO;
        e->automatic.mode = V3_ISP_AE_FIXFPS;
        if (minExp) e->automatic.expTime.min = minExp;
        if (maxExp) e->automatic.expTime.max = maxExp;
        if (maxDGain) e->automatic.dGain.max = maxDGain;
        r = sdk.fnSetExposure(ISP_DEV_ID, e);
    }
    free(e);
    sci_log("auto exp %u..%u maxdgain %u (%#x)\n", minExp, maxExp, maxDGain, r);
    return r;
}

static int sci_wb(int autoMode, unsigned short r, unsigned short g, unsigned short b)
{
    if (!sdk.fnGetWB || !sdk.fnSetWB) return -1;
    v3_isp_wb *w = calloc(1, sizeof(*w));
    if (!w) return -1;
    int ret = sdk.fnGetWB(ISP_DEV_ID, w);
    if (!ret) {
        w->bypass = 0;
        w->op = autoMode ? V3_ISP_OP_AUTO : V3_ISP_OP_MANUAL;
        if (!autoMode) {
            w->manual.r = r;
            w->manual.gr = g;
            w->manual.gb = g;
            w->manual.b = b;
        }
        ret = sdk.fnSetWB(ISP_DEV_ID, w);
    }
    free(w);
    return ret;
}

static int sci_satu(int s)
{
    if (!sdk.fnGetSaturation || !sdk.fnSetSaturation) return -1;
    v3_isp_satu a;
    int r = sdk.fnGetSaturation(ISP_DEV_ID, &a);
    if (r) return r;
    a.op = V3_ISP_OP_MANUAL;
    a.manual = (unsigned char)(s < 0 ? 0 : s > 255 ? 255 : s);
    return sdk.fnSetSaturation(ISP_DEV_ID, &a);
}

static int sci_ccm(int on)
{
    if (!sdk.fnGetModuleControl || !sdk.fnSetModuleControl) return -1;
    unsigned int m;
    int r = sdk.fnGetModuleControl(ISP_DEV_ID, &m);
    if (r) return r;
    if (on) m &= ~V3_ISP_BYPASS_CCM;
    else m |= V3_ISP_BYPASS_CCM;
    return sdk.fnSetModuleControl(ISP_DEV_ID, &m);
}

static int sci_fps(double fps)
{
    if (!sdk.fnGetDeviceConfig || !sdk.fnSetDeviceConfig || !(fps > 1.0 && fps < 60.0)) return -1;
    v3_isp_dev a;
    int r = sdk.fnGetDeviceConfig(ISP_DEV_ID, &a);
    if (r) return r;
    float before = a.framerate;
    a.framerate = (float)fps;
    r = sdk.fnSetDeviceConfig(ISP_DEV_ID, &a);
    sci_log("ISP frame rate %.4f -> %.4f (%#x)\n", before, fps, r);
    return r;
}

static int sci_mono_window(void)
{
    if (isnan(conf.monoFromH) || isnan(conf.monoToH)) return 0;
    time_t t = time(NULL);
    double h = (t % 86400) / 3600.0;
    return conf.monoFromH <= conf.monoToH ? (h >= conf.monoFromH && h < conf.monoToH)
                                          : (h >= conf.monoFromH || h < conf.monoToH);
}

// Night baseline: fixed exposure and gains, unity WB, no colour. Day: auto
// exposure at the fixed frame rate, auto WB, normal saturation.
static void sci_apply_mode(int toNight)
{
    if (toNight) {
        sci_manual(conf.nightAGain, conf.nightDGain, conf.nightIspDGain, conf.nightExp);
        sci_wb(0, 256, 256, 256);
        sci_ccm(1);
        satuNow = sci_mono_window() ? 0 : conf.nightSatu;
        sci_satu(satuNow);
    } else {
        sci_auto(30, conf.dayMaxExp, conf.dayMaxDGain);
        sci_wb(1, 0, 0, 0);
        sci_ccm(1);
        satuNow = conf.daySatu;
        sci_satu(satuNow);
    }
    night = toNight;
    sci_log("mode %s\n", toNight ? "night" : "day");
}

// ---------------------------------------------------------------------------
// Sun altitude (low-precision, ~0.1 deg; enough for a -5 deg switch)
// ---------------------------------------------------------------------------

static double sun_alt_deg(double lat, double lon, time_t t)
{
    const double rad = M_PI / 180.0;
    double d = (t - 946728000.0) / 86400.0;         // days since J2000.0
    double g = fmod(357.529 + 0.98560028 * d, 360.0) * rad;
    double q = 280.459 + 0.98564736 * d;
    double L = fmod(q + 1.915 * sin(g) + 0.020 * sin(2 * g), 360.0) * rad;
    double e = (23.439 - 0.00000036 * d) * rad;
    double ra = atan2(cos(e) * sin(L), cos(L));
    double dec = asin(sin(e) * sin(L));
    double gmst = fmod(18.697374558 + 24.06570982441908 * d, 24.0);
    double ha = (gmst * 15.0 + lon) * rad - ra;
    return asin(sin(lat * rad) * sin(dec) + cos(lat * rad) * cos(dec) * cos(ha)) / rad;
}

static void *sci_maint_thread(void *arg)
{
    (void)arg;
    sleep(2);   // let the ISP and AE settle after pipeline start
    if (!isnan(conf.sensorFps)) {
        pthread_mutex_lock(&ispMtx);
        sci_fps(conf.sensorFps);
        pthread_mutex_unlock(&ispMtx);
    }
    for (;;) {
        pthread_mutex_lock(&ispMtx);
        sci_enforce_modules();
        sci_enforce_blc();
        sci_enforce_gamma();
        sci_enforce_vpss();
        if (!isnan(conf.lat) && !isnan(conf.lon)) {
            double alt = sun_alt_deg(conf.lat, conf.lon, time(NULL));
            int sunNight = alt < conf.switchDeg;
            // Act on sun transitions only, so explicit :9600 commands hold until the next one.
            if (sunNight != lastSunNight) {
                sci_log("sun %.2f deg -> %s\n", alt, sunNight ? "night" : "day");
                sci_apply_mode(sunNight);
                lastSunNight = sunNight;
            }
        } else if (night < 0) {
            sci_apply_mode(1);
        }
        if (night == 1) {
            int want = sci_mono_window() ? 0 : conf.nightSatu;
            if (want != satuNow) {
                sci_log("night saturation %d -> %d\n", satuNow, want);
                sci_satu(want);
                satuNow = want;
            }
        }
        pthread_mutex_unlock(&ispMtx);
        sleep(10);
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// RMSP v5 SEI
// ---------------------------------------------------------------------------

static void put_u32(unsigned char *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void put_u16(unsigned char *p, uint16_t v)
{
    p[0] = v; p[1] = v >> 8;
}

int science_sei(unsigned char *out, size_t max, unsigned long long ptsUs,
    unsigned int seq, int qp, int startQp)
{
    unsigned char rec[61] = { 'R', 'M', 'S', 'P', 5, 0 };
    unsigned char flags = 0;
    struct timeval tv;
    unsigned long long mono = 0;

    // Wall clock and MPP clock read back to back: RMS reconstructs
    // capture_utc = utc - (mono - raw_pts) and median-filters (utc - mono).
    gettimeofday(&tv, NULL);
    if (sdk.fnGetCurrentPts) sdk.fnGetCurrentPts(&mono);

    put_u32(rec + 6, (uint32_t)tv.tv_sec);
    put_u32(rec + 10, (uint32_t)tv.tv_usec);
    put_u32(rec + 14, seq);
    put_u32(rec + 44, (uint32_t)ptsUs);
    put_u32(rec + 48, (uint32_t)mono);

    if (!pthread_mutex_trylock(&ispMtx)) {
        if (sdk.fnQueryExposure && !sdk.fnQueryExposure(ISP_DEV_ID, &expInfo)) {
            put_u32(rec + 18, expInfo.expTime);
            put_u32(rec + 22, expInfo.aGain);
            put_u32(rec + 26, expInfo.dGain);
            put_u32(rec + 30, expInfo.ispDGain);
            flags |= 0x08;
            if (expInfo.linesPer500ms) {
                uint32_t tRowNs = (uint32_t)(500000000ULL / expInfo.linesPer500ms);
                uint32_t fps100 = expInfo.fps ? expInfo.fps : 2500;
                // fps is the ISP's actual frame rate x100 on HiMPP V3
                uint32_t vmax = (uint32_t)((expInfo.linesPer500ms * 200ULL + fps100 / 2) / fps100);
                put_u32(rec + 52, tRowNs);
                put_u16(rec + 56, (uint16_t)vmax);
                flags |= 0x20;
            }
        }
        v3_isp_wbinfo wb;
        if (sdk.fnQueryWB && !sdk.fnQueryWB(ISP_DEV_ID, &wb)) {
            put_u16(rec + 34, wb.gain.r);
            put_u16(rec + 36, wb.gain.b);
            put_u16(rec + 38, (uint16_t)((wb.gain.gr + wb.gain.gb) / 2));
            put_u16(rec + 40, wb.colorTemp);
            flags |= 0x02;
        }
        pthread_mutex_unlock(&ispMtx);
    }

    time_t now = tv.tv_sec;
    if (now != tempAt) {
        tempC = v3_system_readtemp();
        tempAt = now;
    }
    if (!isnan(tempC)) {
        put_u16(rec + 42, (uint16_t)(int16_t)lroundf(tempC * 10.0f));
        flags |= 0x01;
    }
    if (qp > 0 && qp < 52) {
        rec[58] = (unsigned char)qp;
        rec[59] = (unsigned char)(startQp > 0 && startQp < 52 ? startQp : 0);
        flags |= 0x40;
    }
    rec[5] = flags;

    unsigned char ck = 0;
    for (int i = 4; i < 60; i++) ck ^= rec[i];
    rec[60] = ck;
    for (int i = 4; i < 61; i++) rec[i] ^= 0xFF;

    // SEI RBSP: user_data_unregistered (type 5), size 16 + 61, UUID, record, trailing bits
    static const unsigned char uuid[16] = "science-frame-v5";
    unsigned char rbsp[2 + 16 + 61 + 1];
    size_t n = 0;
    rbsp[n++] = 5;
    rbsp[n++] = 16 + sizeof(rec);
    memcpy(rbsp + n, uuid, 16); n += 16;
    memcpy(rbsp + n, rec, sizeof(rec)); n += sizeof(rec);
    rbsp[n++] = 0x80;

    if (max < 5 + 2 * n) return 0;
    size_t o = 0;
    out[o++] = 0; out[o++] = 0; out[o++] = 0; out[o++] = 1;
    out[o++] = 0x06;                                  // nal_ref_idc 0, type 6 (SEI)
    int zeros = 0;
    for (size_t i = 0; i < n; i++) {                  // emulation prevention
        if (zeros >= 2 && rbsp[i] <= 3) {
            out[o++] = 3;
            zeros = 0;
        }
        out[o++] = rbsp[i];
        zeros = rbsp[i] ? 0 : zeros + 1;
    }
    return (int)o;
}

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------

void science_venc_created(int chn)
{
    if (!sdk.fnGetH264Dblk) sci_resolve();
    if (!sdk.fnGetH264Dblk || !sdk.fnSetH264Dblk) return;
    v3_venc_h264dblk d;
    memset(&d, 0, sizeof(d));
    int r = sdk.fnGetH264Dblk(chn, &d);
    sci_log("chn %d deblock was idc=%u a=%d b=%d (%#x)\n", chn, d.disableIdc,
        d.alphaOffsetDiv2, d.betaOffsetDiv2, r);
    d.disableIdc = 1;
    d.alphaOffsetDiv2 = 0;
    d.betaOffsetDiv2 = 0;
    r = sdk.fnSetH264Dblk(chn, &d);
    sci_log("chn %d deblocking filter off (%#x)\n", chn, r);

    sci_load_conf();
    if (conf.chromaQpOffset && sdk.fnGetH264Trans && sdk.fnSetH264Trans) {
        v3_venc_h264trans t;
        memset(&t, 0, sizeof(t));
        r = sdk.fnGetH264Trans(chn, &t);
        sci_log("chn %d trans intra=%u inter=%u scaling=%u chroma_qp=%d (%#x)\n", chn,
            t.intraTransMode, t.interTransMode, t.scalingListOn, t.chromaQpOffset, r);
        // Only write if the read-back looks like the expected layout
        if (!r && t.intraTransMode <= 2 && t.interTransMode <= 2 && t.scalingListOn <= 1 &&
            t.chromaQpOffset >= -12 && t.chromaQpOffset <= 12) {
            t.chromaQpOffset = conf.chromaQpOffset < -12 ? -12 : conf.chromaQpOffset > 12 ? 12 : conf.chromaQpOffset;
            r = sdk.fnSetH264Trans(chn, &t);
            sci_log("chn %d chroma QP offset %+d (%#x)\n", chn, t.chromaQpOffset, r);
        } else {
            sci_log("chn %d trans layout unexpected, chroma QP offset not set\n", chn);
        }
    }
}

// ---------------------------------------------------------------------------
// isp_ctl-compatible control server on :9600
// ---------------------------------------------------------------------------

static int arg_u32(char **tok, int n, const char *flag, unsigned int *val)
{
    for (int i = 0; i + 1 < n; i++)
        if (!strcmp(tok[i], flag)) {
            *val = (unsigned int)strtoul(tok[i + 1], NULL, 10);
            return 1;
        }
    return 0;
}

static void sci_query(char *buf, size_t len)
{
    v3_isp_exp *e = calloc(1, sizeof(*e));
    v3_isp_wbinfo wb;
    unsigned int m = 0;
    int haveExp = sdk.fnQueryExposure && !sdk.fnQueryExposure(ISP_DEV_ID, &expInfo);
    int haveAttr = e && sdk.fnGetExposure && !sdk.fnGetExposure(ISP_DEV_ID, e);
    int haveWb = sdk.fnQueryWB && !sdk.fnQueryWB(ISP_DEV_ID, &wb);
    if (sdk.fnGetModuleControl) sdk.fnGetModuleControl(ISP_DEV_ID, &m);
    v3_isp_black blc = { { 0, 0, 0, 0 } };
    if (sdk.fnGetBlackLevel) sdk.fnGetBlackLevel(ISP_DEV_ID, &blc);
    float t = v3_system_readtemp();
    snprintf(buf, len,
        "Mode: %s\n"
        "AE: %s\n"
        "Exposure: %u us\n"
        "AGain: %u\n"
        "DGain: %u\n"
        "ISPDGain: %u\n"
        "AE Route: fixed frame rate, lines/500ms %u, fps %u\n"
        "WB: R %u Gr %u Gb %u B %u (x256), colortemp %u K\n"
        "SocTemp: %.1f C\n"
        "ModuleControl: %#x (NR %d sharpen %d DPC %d DRC %d bypassed)\n"
        "BlackLevel: %u %u %u %u\n"
        "Science: gamma 0.5 above pedestal %d, deblocking off, all-intra, RMSP v5 SEI\n",
        night < 0 ? "unknown" : night ? "night" : "day",
        haveAttr ? (e->op == V3_ISP_OP_MANUAL ? "manual" : "auto") : "?",
        haveExp ? expInfo.expTime : 0, haveExp ? expInfo.aGain : 0,
        haveExp ? expInfo.dGain : 0, haveExp ? expInfo.ispDGain : 0,
        haveExp ? expInfo.linesPer500ms : 0, haveExp ? expInfo.fps : 0,
        haveWb ? wb.gain.r : 0, haveWb ? wb.gain.gr : 0, haveWb ? wb.gain.gb : 0,
        haveWb ? wb.gain.b : 0, haveWb ? wb.colorTemp : 0, t,
        m, !!(m & V3_ISP_BYPASS_NR), !!(m & V3_ISP_BYPASS_SHARPEN), !!(m & V3_ISP_BYPASS_DPC),
        !!(m & V3_ISP_BYPASS_DRC),
        blc.level[0], blc.level[1], blc.level[2], blc.level[3],
        sciGammaPedestal);
    free(e);
}

static void sci_command(char *line, char *resp, size_t len)
{
    char *tok[24];
    int n = 0;
    for (char *s = strtok(line, " \t\r\n"); s && n < 24; s = strtok(NULL, " \t\r\n"))
        tok[n++] = s;
    if (!n || !strcmp(tok[0], "query")) {
        sci_query(resp, len);
        return;
    }
    const char *c = tok[0];
    int r = 0;

    if (!strcmp(c, "manual")) {
        unsigned int a = conf.nightAGain, d = conf.nightDGain, i = conf.nightIspDGain, e = conf.nightExp;
        if (sdk.fnQueryExposure && !sdk.fnQueryExposure(ISP_DEV_ID, &expInfo)) {
            a = expInfo.aGain; d = expInfo.dGain;
            i = expInfo.ispDGain; e = expInfo.expTime;
        }
        arg_u32(tok, n, "-a", &a);
        arg_u32(tok, n, "-d", &d);
        arg_u32(tok, n, "-i", &i);
        arg_u32(tok, n, "-e", &e);
        r = sci_manual(a, d, i, e);
        snprintf(resp, len, "manual a=%u d=%u i=%u e=%u: %s\n", a, d, i, e, r ? "FAIL" : "OK");
    } else if (!strcmp(c, "auto")) {
        unsigned int mn = 0, mx = 0, dg = 0;
        arg_u32(tok, n, "--min-exptime", &mn);
        arg_u32(tok, n, "--max-exptime", &mx);
        arg_u32(tok, n, "--max-dgain", &dg);
        r = sci_auto(mn, mx, dg);
        snprintf(resp, len, "auto: %s\n", r ? "FAIL" : "OK");
    } else if (!strcmp(c, "wb")) {
        if (n >= 4)
            r = sci_wb(0, (unsigned short)atoi(tok[1]), (unsigned short)atoi(tok[2]), (unsigned short)atoi(tok[3]));
        else if (n >= 2 && !strcmp(tok[1], "auto"))
            r = sci_wb(1, 0, 0, 0);
        else
            r = sci_wb(0, 256, 256, 256);
        snprintf(resp, len, "wb: %s\n", r ? "FAIL" : "OK");
    } else if (!strcmp(c, "satu") && n >= 2) {
        r = sci_satu(atoi(tok[1]));
        snprintf(resp, len, "satu %s: %s\n", tok[1], r ? "FAIL" : "OK");
    } else if (!strcmp(c, "fps") && n >= 2) {
        r = sci_fps(atof(tok[1]));
        snprintf(resp, len, "fps %s: %s\n", tok[1], r ? "FAIL" : "OK");
    } else if (!strcmp(c, "ccm") && n >= 2) {
        r = sci_ccm(!strcmp(tok[1], "on"));
        snprintf(resp, len, "ccm %s: %s\n", tok[1], r ? "FAIL" : "OK");
    } else if (!strcmp(c, "dpc") && n >= 2) {
        conf.dpcOn = !strcmp(tok[1], "on");
        sci_enforce_modules();
        snprintf(resp, len, "dpc %s: OK\n", conf.dpcOn ? "on" : "off");
    } else if (!strcmp(c, "blc") && n >= 2) {
        v3_isp_black b = { { 0, 0, 0, 0 } };
        if (!strcmp(tok[1], "set") && n >= 3) {
            conf.blackLevel = atoi(tok[2]);
            sci_enforce_blc();
        }
        r = sdk.fnGetBlackLevel ? sdk.fnGetBlackLevel(ISP_DEV_ID, &b) : -1;
        snprintf(resp, len, "blc %u %u %u %u (conf %d): %s\n", b.level[0], b.level[1],
            b.level[2], b.level[3], conf.blackLevel, r ? "FAIL" : "OK");
    } else if (!strcmp(c, "pedestal") && n >= 2) {
        conf.pedestal = atoi(tok[1]);
        sci_enforce_gamma();
        snprintf(resp, len, "pedestal %d: OK\n", sciGammaPedestal);
    } else if (!strcmp(c, "modctrl") && n >= 2) {
        unsigned int m = 0;
        if (!strcmp(tok[1], "set") && n >= 3 && sdk.fnSetModuleControl) {
            m = (unsigned int)strtoul(tok[2], NULL, 16);
            r = sdk.fnSetModuleControl(ISP_DEV_ID, &m);
        }
        if (sdk.fnGetModuleControl) sdk.fnGetModuleControl(ISP_DEV_ID, &m);
        snprintf(resp, len, "modctrl %#x (enforce %s): %s\n", m, enforceOn ? "on" : "off", r ? "FAIL" : "OK");
    } else if (!strcmp(c, "enforce") && n >= 2) {
        enforceOn = !strcmp(tok[1], "on");
        if (enforceOn) sci_enforce_modules();
        snprintf(resp, len, "enforce %s: OK\n", enforceOn ? "on" : "off");
    } else if (!strcmp(c, "ircut") && n >= 2) {
        night = !strcmp(tok[1], "night");
        snprintf(resp, len, "ircut %s: OK (mode only, no IR-cut actuator driven)\n", tok[1]);
    } else if (!strcmp(c, "time") && n >= 2) {
        if (!strcmp(tok[1], "set") && n >= 3) {
            struct timeval tv = { .tv_sec = (time_t)strtoll(tok[2], NULL, 10) };
            r = settimeofday(&tv, NULL);
            snprintf(resp, len, "time set: %s\n", r ? "FAIL" : "OK");
        } else if (!strcmp(tok[1], "ntp") && n >= 4 && !strcmp(tok[2], "host")) {
            FILE *f = fopen("/etc/ntp.conf", "w");
            if (f) {
                fprintf(f, "server %s iburst\n", tok[3]);
                fclose(f);
                r = system("/etc/init.d/S49ntpd restart >/dev/null 2>&1");
            } else r = -1;
            snprintf(resp, len, "time ntp host %s: %s\n", tok[3], r ? "FAIL" : "OK");
        } else {
            time_t t = time(NULL);
            struct tm tm;
            gmtime_r(&t, &tm);
            strftime(resp, len, "time: %Y-%m-%d %H:%M:%S UTC\n", &tm);
        }
    } else if (!strcmp(c, "reboot")) {
        snprintf(resp, len, "reboot: OK\n");
        sync();
        if (fork() == 0) {
            sleep(1);
            execl("/sbin/reboot", "reboot", NULL);
            _exit(1);
        }
    } else {
        // encode/osd/flip/mirror/colortone/cloud/autoreboot: fixed at startup on this build
        snprintf(resp, len, "%s: ignored (fixed in divinus.yaml / not applicable)\n", c);
    }
}

static void *sci_server_thread(void *arg)
{
    (void)arg;
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(SCI_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof(a)) || listen(s, 4)) {
        sci_log("cannot listen on :%d\n", SCI_PORT);
        return NULL;
    }
    sci_log("control server on :%d\n", SCI_PORT);
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) continue;
        struct timeval to = { .tv_sec = 3 };
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
        char line[512];
        size_t got = 0;
        ssize_t k;
        while (got < sizeof(line) - 1 && (k = recv(c, line + got, sizeof(line) - 1 - got, 0)) > 0) {
            got += (size_t)k;
            if (memchr(line, '\n', got)) break;
        }
        line[got] = 0;
        char *resp = malloc(2048);
        if (resp) {
            pthread_mutex_lock(&ispMtx);
            sci_command(line, resp, 2048);
            pthread_mutex_unlock(&ispMtx);
            send(c, resp, strlen(resp), MSG_NOSIGNAL);
            free(resp);
        }
        close(c);
    }
    return NULL;
}

void science_start(void)
{
    pthread_t t;
    sci_resolve();
    sci_load_conf();
    pthread_create(&t, NULL, sci_maint_thread, NULL);
    pthread_detach(t);
    pthread_create(&t, NULL, sci_server_thread, NULL);
    pthread_detach(t);
}

#endif

#pragma once

// HiMPP V3 (Hi3516CV300) ISP, VPSS and VENC parameters that science mode reads and
// writes through calls the v3 HAL does not otherwise make. Only the members science mode
// uses carry names; the rest of each structure is reserved space, sized so the vendor
// libraries can fill the whole structure. Sizes, alignments and member offsets are
// checked against the vendor headers by tools/science/abi_check.c.

#include "v3_isp.h"
#include "v3_vpss.h"

// The ISP group attribute (HI_MPI_ISP_Get/SetPubAttr) is v3_isp_dev, the VPSS group
// attribute (HI_MPI_VPSS_Get/SetGrpAttr) is v3_vpss_grp; both come from the v3 HAL.

// Module control word (HI_MPI_ISP_Get/SetModuleControl): a set bit bypasses the block.
#define V3_ISP_BYPASS_ANTIFC    (1u << 3)
#define V3_ISP_BYPASS_CROSSTALK (1u << 4)
#define V3_ISP_BYPASS_DPC       (1u << 5)
#define V3_ISP_BYPASS_NR        (1u << 6)
#define V3_ISP_BYPASS_DEHAZE    (1u << 7)
#define V3_ISP_BYPASS_SHADING   (1u << 9)
#define V3_ISP_BYPASS_DRC       (1u << 11)
#define V3_ISP_BYPASS_CCM       (1u << 13)
#define V3_ISP_BYPASS_GAMMA     (1u << 14)
#define V3_ISP_BYPASS_CA        (1u << 16)
#define V3_ISP_BYPASS_SHARPEN   (1u << 22)
#define V3_ISP_BYPASS_CAC       (1u << 23)

typedef enum {
    V3_ISP_OP_AUTO,
    V3_ISP_OP_MANUAL,
    V3_ISP_OP_END
} v3_isp_op;

typedef enum {
    V3_ISP_AE_SLOWSHUT,
    V3_ISP_AE_FIXFPS,
    V3_ISP_AE_END
} v3_isp_aemode;

#define V3_ISP_GAMMA_NODES 257

typedef enum {
    V3_ISP_GAMMA_DEFAULT,
    V3_ISP_GAMMA_SRGB,
    V3_ISP_GAMMA_CUSTOM,
    V3_ISP_GAMMA_END
} v3_isp_gammacurve;

// HI_MPI_ISP_Get/SetGammaAttr: 257 nodes, 12-bit in and out, 16 input codes apart
typedef struct {
    int enable;
    v3_isp_gammacurve curve;
    unsigned short table[V3_ISP_GAMMA_NODES];
} v3_isp_gamma;

// HI_MPI_ISP_Get/SetBlackLevelAttr: 12-bit level per Bayer channel
typedef struct {
    unsigned short level[4];
} v3_isp_black;

typedef struct {
    unsigned int max;
    unsigned int min;
} v3_isp_range;

// Manual exposure: times in us, gains x1024
typedef struct {
    v3_isp_op expTimeOp;
    v3_isp_op aGainOp;
    v3_isp_op dGainOp;
    v3_isp_op ispDGainOp;
    unsigned int expTime;
    unsigned int aGain;
    unsigned int dGain;
    unsigned int ispDGain;
} v3_isp_expman;

typedef struct {
    v3_isp_range expTime;
    v3_isp_range aGain;
    v3_isp_range dGain;
    v3_isp_range ispDGain;
    v3_isp_range sysGain;
    // Gain threshold, speed, tolerance, target, EV bias, strategy, histogram terms
    unsigned int reserved0[5];
    v3_isp_aemode mode;
    // Anti-flicker, delays, manual exposure value, WDR options, zone weights
    unsigned int reserved1[74];
} v3_isp_expauto;

// HI_MPI_ISP_Get/SetExposureAttr
typedef struct {
    int bypass;
    v3_isp_op op;
    // Run interval, histogram adjustment, extended route switch
    unsigned int reserved[3];
    v3_isp_expman manual;
    v3_isp_expauto automatic;
} v3_isp_exp;

// HI_MPI_ISP_QueryExposureInfo (4.8 KB, keep it off small stacks)
typedef struct {
    unsigned int expTime;
    unsigned int reserved0;
    unsigned int aGain;
    unsigned int dGain;
    unsigned int ispDGain;
    // Total exposure, limit flag, histogram error, 1024- and 5-bin histograms, mean luma
    unsigned int reserved1[1030];
    unsigned int linesPer500ms;
    unsigned int reserved2;
    unsigned int fps;               // actual frame rate x100
    // ISO, WDR ratio, first stable time, the AE routes in force
    unsigned int reserved3[165];
} v3_isp_expinfo;

// White balance gains, x256
typedef struct {
    unsigned short r;
    unsigned short gr;
    unsigned short gb;
    unsigned short b;
} v3_isp_wbgain;

// HI_MPI_ISP_Get/SetWBAttr
typedef struct {
    int bypass;
    unsigned char reserved0;        // run interval
    v3_isp_op op;
    v3_isp_wbgain manual;
    // Automatic white balance tuning
    unsigned int reserved1[61];
} v3_isp_wb;

// HI_MPI_ISP_QueryWBInfo
typedef struct {
    v3_isp_wbgain gain;
    unsigned short saturation;
    unsigned short colorTemp;       // K
    // Colour matrix in force, light source statistics, indoor/outdoor state
    unsigned short reserved0[16];
    unsigned int reserved1;
} v3_isp_wbinfo;

// HI_MPI_ISP_Get/SetSaturationAttr
typedef struct {
    v3_isp_op op;
    unsigned char manual;
    unsigned char reserved[16];     // automatic values per ISO step
} v3_isp_satu;

// HI_MPI_VENC_Get/SetH264Dblk
typedef struct {
    unsigned int disableIdc;        // 0 on, 1 off, 2 off across slice edges only
    int alphaOffsetDiv2;
    int betaOffsetDiv2;
} v3_venc_h264dblk;

// HI_MPI_VENC_Get/SetH264Trans
typedef struct {
    unsigned int intraTransMode;    // 0 4x4 and 8x8, 1 4x4, 2 8x8
    unsigned int interTransMode;
    unsigned int scalingListOn;
    unsigned char interScaling8x8[64];
    unsigned char intraScaling8x8[64];
    int chromaQpOffset;             // -12..12
} v3_venc_h264trans;

// Entry points resolved at run time from the already loaded MPI libraries
typedef struct {
    int (*fnGetModuleControl)(int device, unsigned int *bypass);
    int (*fnSetModuleControl)(int device, const unsigned int *bypass);
    int (*fnGetGamma)(int device, v3_isp_gamma *config);
    int (*fnSetGamma)(int device, const v3_isp_gamma *config);
    int (*fnGetExposure)(int device, v3_isp_exp *config);
    int (*fnSetExposure)(int device, const v3_isp_exp *config);
    int (*fnQueryExposure)(int device, v3_isp_expinfo *info);
    int (*fnGetWB)(int device, v3_isp_wb *config);
    int (*fnSetWB)(int device, const v3_isp_wb *config);
    int (*fnQueryWB)(int device, v3_isp_wbinfo *info);
    int (*fnGetSaturation)(int device, v3_isp_satu *config);
    int (*fnSetSaturation)(int device, const v3_isp_satu *config);
    int (*fnGetDeviceConfig)(int device, v3_isp_dev *config);
    int (*fnSetDeviceConfig)(int device, const v3_isp_dev *config);
    int (*fnGetBlackLevel)(int device, v3_isp_black *config);
    int (*fnSetBlackLevel)(int device, const v3_isp_black *config);
    int (*fnGetGroupConfig)(int group, v3_vpss_grp *config);
    int (*fnSetGroupConfig)(int group, v3_vpss_grp *config);
    int (*fnGetH264Dblk)(int channel, v3_venc_h264dblk *config);
    int (*fnSetH264Dblk)(int channel, const v3_venc_h264dblk *config);
    int (*fnGetH264Trans)(int channel, v3_venc_h264trans *config);
    int (*fnSetH264Trans)(int channel, const v3_venc_h264trans *config);
    int (*fnGetCurrentPts)(unsigned long long *pts);
} v3_science_impl;

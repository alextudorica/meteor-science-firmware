// Compile-time check that the science mode declarations in src/hal/hisi/v3_science.h
// (and the v3 HAL types it reuses) lay out memory exactly as the Hi3516CV300 MPI
// libraries expect. Nothing here is linked into divinus; run abi_check.sh with the
// target compiler and the vendor include directory. Any mismatch stops the build.

#include <stddef.h>

#include "hi_comm_isp.h"
#include "hi_comm_venc.h"
#include "hi_comm_vpss.h"

#include "v3_science.h"

#define SAME_TYPE(ours, theirs) \
    _Static_assert(sizeof(ours) == sizeof(theirs), "size of " #ours); \
    _Static_assert(_Alignof(ours) == _Alignof(theirs), "alignment of " #ours)

#define SAME_MEMBER(ours, om, theirs, tm) \
    _Static_assert(offsetof(ours, om) == offsetof(theirs, tm), "offset of " #ours "." #om); \
    _Static_assert(sizeof(((ours *)0)->om) == sizeof(((theirs *)0)->tm), "size of " #ours "." #om)

#define SAME_VALUE(ours, theirs) \
    _Static_assert((long)(ours) == (long)(theirs), "value of " #ours)

// Enumerations
SAME_TYPE(v3_isp_op, ISP_OP_TYPE_E);
SAME_VALUE(V3_ISP_OP_AUTO, OP_TYPE_AUTO);
SAME_VALUE(V3_ISP_OP_MANUAL, OP_TYPE_MANUAL);
SAME_TYPE(v3_isp_aemode, ISP_AE_MODE_E);
SAME_VALUE(V3_ISP_AE_SLOWSHUT, AE_MODE_SLOW_SHUTTER);
SAME_VALUE(V3_ISP_AE_FIXFPS, AE_MODE_FIX_FRAME_RATE);
SAME_TYPE(v3_isp_gammacurve, ISP_GAMMA_CURVE_TYPE_E);
SAME_VALUE(V3_ISP_GAMMA_DEFAULT, ISP_GAMMA_CURVE_DEFAULT);
SAME_VALUE(V3_ISP_GAMMA_SRGB, ISP_GAMMA_CURVE_SRGB);
SAME_VALUE(V3_ISP_GAMMA_CUSTOM, ISP_GAMMA_CURVE_USER_DEFINE);
SAME_VALUE(V3_ISP_GAMMA_NODES, GAMMA_NODE_NUM);

// Module control word
SAME_TYPE(unsigned int, ISP_MODULE_CTRL_U);

// Gamma
SAME_TYPE(v3_isp_gamma, ISP_GAMMA_ATTR_S);
SAME_MEMBER(v3_isp_gamma, enable, ISP_GAMMA_ATTR_S, bEnable);
SAME_MEMBER(v3_isp_gamma, curve, ISP_GAMMA_ATTR_S, enCurveType);
SAME_MEMBER(v3_isp_gamma, table, ISP_GAMMA_ATTR_S, u16Table);

// Black level
SAME_TYPE(v3_isp_black, ISP_BLACK_LEVEL_S);
SAME_MEMBER(v3_isp_black, level, ISP_BLACK_LEVEL_S, au16BlackLevel);

// Exposure attribute
SAME_TYPE(v3_isp_range, ISP_AE_RANGE_S);
SAME_MEMBER(v3_isp_range, max, ISP_AE_RANGE_S, u32Max);
SAME_MEMBER(v3_isp_range, min, ISP_AE_RANGE_S, u32Min);
SAME_TYPE(v3_isp_expman, ISP_ME_ATTR_S);
SAME_MEMBER(v3_isp_expman, expTimeOp, ISP_ME_ATTR_S, enExpTimeOpType);
SAME_MEMBER(v3_isp_expman, aGainOp, ISP_ME_ATTR_S, enAGainOpType);
SAME_MEMBER(v3_isp_expman, dGainOp, ISP_ME_ATTR_S, enDGainOpType);
SAME_MEMBER(v3_isp_expman, ispDGainOp, ISP_ME_ATTR_S, enISPDGainOpType);
SAME_MEMBER(v3_isp_expman, expTime, ISP_ME_ATTR_S, u32ExpTime);
SAME_MEMBER(v3_isp_expman, aGain, ISP_ME_ATTR_S, u32AGain);
SAME_MEMBER(v3_isp_expman, dGain, ISP_ME_ATTR_S, u32DGain);
SAME_MEMBER(v3_isp_expman, ispDGain, ISP_ME_ATTR_S, u32ISPDGain);
SAME_TYPE(v3_isp_expauto, ISP_AE_ATTR_S);
SAME_MEMBER(v3_isp_expauto, expTime, ISP_AE_ATTR_S, stExpTimeRange);
SAME_MEMBER(v3_isp_expauto, aGain, ISP_AE_ATTR_S, stAGainRange);
SAME_MEMBER(v3_isp_expauto, dGain, ISP_AE_ATTR_S, stDGainRange);
SAME_MEMBER(v3_isp_expauto, ispDGain, ISP_AE_ATTR_S, stISPDGainRange);
SAME_MEMBER(v3_isp_expauto, sysGain, ISP_AE_ATTR_S, stSysGainRange);
SAME_MEMBER(v3_isp_expauto, mode, ISP_AE_ATTR_S, enAEMode);
SAME_TYPE(v3_isp_exp, ISP_EXPOSURE_ATTR_S);
SAME_MEMBER(v3_isp_exp, bypass, ISP_EXPOSURE_ATTR_S, bByPass);
SAME_MEMBER(v3_isp_exp, op, ISP_EXPOSURE_ATTR_S, enOpType);
SAME_MEMBER(v3_isp_exp, manual, ISP_EXPOSURE_ATTR_S, stManual);
SAME_MEMBER(v3_isp_exp, automatic, ISP_EXPOSURE_ATTR_S, stAuto);

// Exposure state
SAME_TYPE(v3_isp_expinfo, ISP_EXP_INFO_S);
SAME_MEMBER(v3_isp_expinfo, expTime, ISP_EXP_INFO_S, u32ExpTime);
SAME_MEMBER(v3_isp_expinfo, aGain, ISP_EXP_INFO_S, u32AGain);
SAME_MEMBER(v3_isp_expinfo, dGain, ISP_EXP_INFO_S, u32DGain);
SAME_MEMBER(v3_isp_expinfo, ispDGain, ISP_EXP_INFO_S, u32ISPDGain);
SAME_MEMBER(v3_isp_expinfo, linesPer500ms, ISP_EXP_INFO_S, u32LinesPer500ms);
SAME_MEMBER(v3_isp_expinfo, fps, ISP_EXP_INFO_S, u32Fps);

// White balance attribute and state
SAME_TYPE(v3_isp_wbgain, ISP_MWB_ATTR_S);
SAME_MEMBER(v3_isp_wbgain, r, ISP_MWB_ATTR_S, u16Rgain);
SAME_MEMBER(v3_isp_wbgain, gr, ISP_MWB_ATTR_S, u16Grgain);
SAME_MEMBER(v3_isp_wbgain, gb, ISP_MWB_ATTR_S, u16Gbgain);
SAME_MEMBER(v3_isp_wbgain, b, ISP_MWB_ATTR_S, u16Bgain);
SAME_TYPE(v3_isp_wb, ISP_WB_ATTR_S);
SAME_MEMBER(v3_isp_wb, bypass, ISP_WB_ATTR_S, bByPass);
SAME_MEMBER(v3_isp_wb, op, ISP_WB_ATTR_S, enOpType);
SAME_MEMBER(v3_isp_wb, manual, ISP_WB_ATTR_S, stManual);
SAME_TYPE(v3_isp_wbinfo, ISP_WB_INFO_S);
SAME_MEMBER(v3_isp_wbinfo, gain.r, ISP_WB_INFO_S, u16Rgain);
SAME_MEMBER(v3_isp_wbinfo, gain.gr, ISP_WB_INFO_S, u16Grgain);
SAME_MEMBER(v3_isp_wbinfo, gain.gb, ISP_WB_INFO_S, u16Gbgain);
SAME_MEMBER(v3_isp_wbinfo, gain.b, ISP_WB_INFO_S, u16Bgain);
SAME_MEMBER(v3_isp_wbinfo, saturation, ISP_WB_INFO_S, u16Saturation);
SAME_MEMBER(v3_isp_wbinfo, colorTemp, ISP_WB_INFO_S, u16ColorTemp);

// Saturation
SAME_TYPE(v3_isp_satu, ISP_SATURATION_ATTR_S);
SAME_MEMBER(v3_isp_satu, op, ISP_SATURATION_ATTR_S, enOpType);
SAME_MEMBER(v3_isp_satu, manual, ISP_SATURATION_ATTR_S, stManual.u8Saturation);
SAME_MEMBER(v3_isp_satu, reserved, ISP_SATURATION_ATTR_S, stAuto.au8Sat);

// ISP group attribute, reused from the v3 HAL
SAME_TYPE(v3_isp_dev, ISP_PUB_ATTR_S);
SAME_MEMBER(v3_isp_dev, capt.x, ISP_PUB_ATTR_S, stWndRect.s32X);
SAME_MEMBER(v3_isp_dev, capt.y, ISP_PUB_ATTR_S, stWndRect.s32Y);
SAME_MEMBER(v3_isp_dev, capt.width, ISP_PUB_ATTR_S, stWndRect.u32Width);
SAME_MEMBER(v3_isp_dev, capt.height, ISP_PUB_ATTR_S, stWndRect.u32Height);
SAME_MEMBER(v3_isp_dev, framerate, ISP_PUB_ATTR_S, f32FrameRate);
SAME_MEMBER(v3_isp_dev, bayer, ISP_PUB_ATTR_S, enBayer);

// VPSS group attribute, reused from the v3 HAL
SAME_TYPE(v3_vpss_grp, VPSS_GRP_ATTR_S);
SAME_MEMBER(v3_vpss_grp, dest.width, VPSS_GRP_ATTR_S, u32MaxW);
SAME_MEMBER(v3_vpss_grp, dest.height, VPSS_GRP_ATTR_S, u32MaxH);
SAME_MEMBER(v3_vpss_grp, pixFmt, VPSS_GRP_ATTR_S, enPixFmt);
SAME_MEMBER(v3_vpss_grp, enhOn, VPSS_GRP_ATTR_S, bIeEn);
SAME_MEMBER(v3_vpss_grp, dciOn, VPSS_GRP_ATTR_S, bDciEn);
SAME_MEMBER(v3_vpss_grp, nredOn, VPSS_GRP_ATTR_S, bNrEn);
SAME_MEMBER(v3_vpss_grp, histEn, VPSS_GRP_ATTR_S, bHistEn);
SAME_MEMBER(v3_vpss_grp, interlMode, VPSS_GRP_ATTR_S, enDieMode);
SAME_MEMBER(v3_vpss_grp, sharpOn, VPSS_GRP_ATTR_S, bSharpenEn);

// H.264 deblocking and transform parameters
SAME_TYPE(v3_venc_h264dblk, VENC_PARAM_H264_DBLK_S);
SAME_MEMBER(v3_venc_h264dblk, disableIdc, VENC_PARAM_H264_DBLK_S, disable_deblocking_filter_idc);
SAME_MEMBER(v3_venc_h264dblk, alphaOffsetDiv2, VENC_PARAM_H264_DBLK_S, slice_alpha_c0_offset_div2);
SAME_MEMBER(v3_venc_h264dblk, betaOffsetDiv2, VENC_PARAM_H264_DBLK_S, slice_beta_offset_div2);
SAME_TYPE(v3_venc_h264trans, VENC_PARAM_H264_TRANS_S);
SAME_MEMBER(v3_venc_h264trans, intraTransMode, VENC_PARAM_H264_TRANS_S, u32IntraTransMode);
SAME_MEMBER(v3_venc_h264trans, interTransMode, VENC_PARAM_H264_TRANS_S, u32InterTransMode);
SAME_MEMBER(v3_venc_h264trans, scalingListOn, VENC_PARAM_H264_TRANS_S, bScalingListValid);
SAME_MEMBER(v3_venc_h264trans, interScaling8x8, VENC_PARAM_H264_TRANS_S, InterScalingList8X8);
SAME_MEMBER(v3_venc_h264trans, intraScaling8x8, VENC_PARAM_H264_TRANS_S, IntraScalingList8X8);
SAME_MEMBER(v3_venc_h264trans, chromaQpOffset, VENC_PARAM_H264_TRANS_S, chroma_qp_index_offset);

// Bit-fields have no offsetof, so the module control bits are compared after constant
// folding: a mismatch leaves a call to bypass_bit_mismatch(), which GCC turns into a
// compile error. Build with optimisation on (abi_check.sh uses -O2).
extern void bypass_bit_mismatch(void) __attribute__((error("module control bit differs")));

#define SAME_BIT(mask, field) do { \
    ISP_MODULE_CTRL_U u = { .u32Key = 0 }; \
    u.field = 1; \
    if (u.u32Key != (mask)) bypass_bit_mismatch(); \
} while (0)

void abi_check_bits(void)
{
    SAME_BIT(V3_ISP_BYPASS_ANTIFC, bitBypassAntiFC);
    SAME_BIT(V3_ISP_BYPASS_CROSSTALK, bitBypassCrosstalkR);
    SAME_BIT(V3_ISP_BYPASS_DPC, bitBypassDPC);
    SAME_BIT(V3_ISP_BYPASS_NR, bitBypassNR);
    SAME_BIT(V3_ISP_BYPASS_DEHAZE, bitBypassDehaze);
    SAME_BIT(V3_ISP_BYPASS_SHADING, bitBypassShading);
    SAME_BIT(V3_ISP_BYPASS_DRC, bitBypassDRC);
    SAME_BIT(V3_ISP_BYPASS_CCM, bitBypassColorMatrix);
    SAME_BIT(V3_ISP_BYPASS_GAMMA, bitBypassGamma);
    SAME_BIT(V3_ISP_BYPASS_CA, bitBypassCA);
    SAME_BIT(V3_ISP_BYPASS_SHARPEN, bitBypassSharpen);
    SAME_BIT(V3_ISP_BYPASS_CAC, bitBypassCAC);
}

#ifndef __MPP_H__
#define __MPP_H__

#include "hink_vpss.h"

typedef struct {
    VPSS_GRP vpssGrp;
    MOD_ID_E srcModId;
    union {
        VI_CHN viChn;
        VDEC_CHN vdeChn;
    };
    HI_BOOL enable[VPSS_MAX_PHY_CHN_NUM];
    PIC_SIZE_E enSize[VPSS_MAX_PHY_CHN_NUM];
}mpp_vpss_t;

int mpp_vpss_start(mpp_vpss_t *vpss);

#endif /*__MPP_H__*/

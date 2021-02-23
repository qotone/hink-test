#include "mpp.h"
#include "hi_comm_video.h"
#include "hi_common.h"
#include "hi_defines.h"
#include "hi_type.h"
#include "hink_sys.h"
#include "hink_vi.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>

int mpp_vpss_start(mpp_vpss_t *vpss)
{

    HI_S32 s32Ret = 0;
    VPSS_GRP_PARAM_S stGrpParam = {0};
    VPSS_GRP_ATTR_S stGrpAttr = {0};
    VPSS_CHN_ATTR_S stChnAttr = {0};
    //SIZE_S stSize = {.u32Width = 1920,.u32Height= 1080};
    SIZE_S stSize[VPSS_MAX_PHY_CHN_NUM];
    int i;

    HINK_VPSS_DEF_GRP_PARAM(stGrpParam);
    HINK_VPSS_DEF_GRP_ATTR(stGrpAttr, 1920, 1080);
    HINK_VPSS_DEF_CHN_ATTR(stChnAttr);

    for(i = 0; i < VPSS_MAX_PHY_CHN_NUM; i++){

        if( !vpss->enable[i])
            continue;

        hink_sys_getPicSize(VIDEO_ENCODING_MODE_PAL, vpss->enSize[i], &stSize[i]);
        //printf("stSize[%d]:width=%d,height=%d\n", i,stSize[i].u32Width,stSize[i].u32Height);
        if(s32Ret != HI_SUCCESS){
            HINK_PRT("hink_sys_getPicSize failed! %#x\n",s32Ret);
            return s32Ret;
        }
    }

    s32Ret = hink_vpss_start(vpss->vpssGrp, vpss->enable,stSize, &stGrpAttr, &stGrpParam, &stChnAttr);
    if(s32Ret != HI_SUCCESS){
        HINK_PRT("hink_vpss_start %#x\n",s32Ret);
        return s32Ret;
    }


    return 0;
}

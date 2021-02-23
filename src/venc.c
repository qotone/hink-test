#include "venc.h"
#include <asm-generic/errno-base.h>
#include <stdio.h>
#include <h264/h264.h>
#include <ulog.h>
/* #include <libpomp.h> */
/* #include "vstrm_test_socket.h" */

#define FILE_POSTFIX    ".h264"
#define VENC_CHN_NUM   (4)
FILE *pFile[VENC_CHN_NUM] = {NULL,};
char aszFileName[64][VENC_CHN_NUM];



/* static int au_parse(struct vstrm_test_sender *self) */
/* { */
/* 	int res = 0; */
/* 	size_t off = 0; */

/* 	res = h264_reader_parse(self->reader, */
/* 				0, */
/* 				(uint8_t *)self->data + self->data_off, */
/* 				self->data_len - self->data_off, */
/* 				&off); */
/* 	if (res < 0) { */
/* 		ULOG_ERRNO("h264_reader_parse", -res); */
/* 		return -res; */
/* 	} */

/* 	self->data_off += off; */
/* 	if (self->data_off >= self->data_len) */
/* 		finish(self); */

/* 	return res; */
/* } */







int mpp_venc_recv(VENC_CHN VeChn, PAYLOAD_TYPE_E PT, VENC_STREAM_S* pstStream, void* uargs)
{

    //struct vstrm_sender_hisi *self = (struct  vstrm_sender_hisi *)uargs;
    struct h264_reader *reader = (struct h264_reader *)uargs;

	ULOG_ERRNO_RETURN_ERR_IF(pstStream == NULL, EINVAL);

    if(VeChn == 0){
        int res = 0;
        int i = 0;
        size_t off;
        HI_U8 *pbuf;
        int data_len = 0;
        int buf_offset = 0;

        for(i = 0; i < pstStream->u32PackCount; i++){
#if 0
            if(pstStream->u32PackCount > 1)
                ULOGI("u32PackCount[%d] > 1 send len =%u",pstStream->u32PackCount,pstStream->pstPack[i].u32Len - pstStream->pstPack[i].u32Offset);
#endif
            res = h264_reader_parse(reader, 0, pstStream->pstPack[i].pu8Addr + pstStream->pstPack[i].u32Offset,pstStream->pstPack[i].u32Len - pstStream->pstPack[i].u32Offset , &off);
            if(res < 0){
                ULOG_ERRNO("h264_reader_parse", -res);
                return -res;
            }

        }
    }else{
        if(pFile[VeChn] == NULL)
        {
            snprintf(aszFileName[VeChn],32, "stream_chn%d_%d%s", VeChn,PT, FILE_POSTFIX);

            pFile[VeChn] = fopen(aszFileName[VeChn], "wb");
            if (!pFile[VeChn])
            {
                HINK_PRT("open file[%s] failed!\n",
                         aszFileName[VeChn]);
                return -1;
            }
        }else {
            int i = 0;
            for (i = 0; i < pstStream->u32PackCount; i++)
            {
                fwrite(pstStream->pstPack[i].pu8Addr + pstStream->pstPack[i].u32Offset,
                       pstStream->pstPack[i].u32Len - pstStream->pstPack[i].u32Offset, 1, pFile[VeChn]);
                fflush(pFile[VeChn]);
            }
        }

    }


    return 0;
}

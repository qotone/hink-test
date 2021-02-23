#ifndef __VENC_H__
#include "hink_venc.h"
#include <inttypes.h>
//#include "vstrm_test_socket.h"

/* struct vstrm_sender_hisi { */
/*     int should_stop; */
/*     struct pomp_loop *loop; */
/*     void *data; */
/*     size_t data_len; */
/*     size_t data_off; */
/*     uint8_t *sps; */
/*     size_t sps_len; */
/*     uint8_t *pps; */
/*     size_t pps_len; */
/*     struct h264_reader *reader; */
/*     struct vstrm_sender *sender; */
/*     struct vstrm_test_socket data_sock; */
/*     struct vstrm_test_socket ctrl_sock; */
/*     struct vstrm_frame *frame; */
/*     uint64_t timestamp; */
/*     void *userdata; */

/* }; */


typedef struct {
    int ch_num;
    int st_num;
}mpp_venc_ini_t;

int mpp_venc_init(mpp_venc_ini_t *ini);

int mpp_venc_recv(VENC_CHN VeChn, PAYLOAD_TYPE_E PT, VENC_STREAM_S* pstStream, void* uargs);

#endif /* __VENC_H__ */

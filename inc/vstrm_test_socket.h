#ifndef __VSTRM_TEST_SOCKET_H__
#define __VSTRM_TEST_SOCKET_H__

#include <inttypes.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <libpomp.h>

struct vstrm_test_socket {
	int fd;
	struct sockaddr_in local_addr;
	struct sockaddr_in remote_addr;
	uint8_t *rxbuf;
	size_t rxbufsize;
};


int vstrm_test_socket_setup(struct vstrm_test_socket *sock,
			    const char *local_addr,
			    uint16_t *local_port,
			    const char *remote_addr,
			    uint16_t remote_port,
			    struct pomp_loop *loop,
			    pomp_fd_event_cb_t fd_cb,
			    void *userdata);


void vstrm_test_socket_cleanup(struct vstrm_test_socket *sock,
			       struct pomp_loop *loop);


int vstrm_test_socket_set_rx_size(struct vstrm_test_socket *sock, size_t size);


int vstrm_test_socket_set_tx_size(struct vstrm_test_socket *sock, size_t size);


int vstrm_test_socket_set_class(struct vstrm_test_socket *sock, int cls);


ssize_t vstrm_test_socket_read(struct vstrm_test_socket *sock);


ssize_t vstrm_test_socket_write(struct vstrm_test_socket *sock,
				const void *buf,
				size_t len);


#endif /*__VSTRM_TEST_SOCKET_H__*/

#include "global.h"
#include "manager.h"
#include "transport.h"
#include "nn.hpp"
#include "mem_alloc.h"
#include "remote_query.h"
#include "query.h"

/*
	 HEADER FORMAT:
	 32b destination ID
	 32b source ID
	 32b ??? [unused] ??? transaction ID
	 ===> 32b message type <=== First 4 bytes of data
	 N?	data

	 */

void Transport::init(uint64_t node_id) {
	printf("Init %ld\n",node_id);
	_node_id = node_id;
//	s.socket(AF_SP, NN_BUS);
	// Format: transport://addr
	int rc;
	char socket_name[MAX_TPORT_NAME];
	strcpy(socket_name,TPORT_TYPE);
	strcat(socket_name,"://");
	strcat(socket_name,TPORT_PORT);

	int timeo = 1000; // timeout in ms
	s.setsockopt(NN_SOL_SOCKET,NN_RCVTIMEO,&timeo,sizeof(timeo));

	if(node_id == 0) {
		rc = s.bind(socket_name);
	}
	else {
		rc = s.connect(socket_name);
	}
	if(rc < 0) {
		printf("Bind Error: %d %s\n",errno,strerror(errno));
	}
}

uint64_t Transport::get_node_id() {
	return _node_id;
}

void Transport::send_msg(uint64_t dest_id, void ** data, int * sizes, int num) {

	// 1: Scrape data pointers for actual data
	// Profile data for size
	uint64_t size = HEADER_SIZE;
	for(int i = 0; i < num; i++) {
		size += sizes[i];
	}
	// For step 3
	size += sizeof(ts_t);

	void * sbuf = nn_allocmsg(size,0);
	memset(sbuf,0,size);

	// Copy all data into sbuf
	uint64_t dsize = HEADER_SIZE;
	for(int i = 0; i < num; i++) {
		memcpy(&((char*)sbuf)[dsize],data[i],sizes[i]);
		dsize += sizes[i];
	}


	// 2: Create message header
	((uint32_t*)sbuf)[0] = dest_id;
	((uint32_t*)sbuf)[1] = get_node_id();

#if DEBUG_DISTR
	printf("Sending %ld -> %ld: %ld bytes\n",get_node_id(),dest_id,size);
#endif

	// 3: Add time of message sending for stats purposes
	ts_t time = get_sys_clock();
	memcpy(&((char*)sbuf)[dsize],&time,sizeof(ts_t));
	dsize += sizeof(ts_t);

	assert(size == dsize);

	int rc;
	
	// 4: send message
	rc= s.send(&sbuf,NN_MSG,0);

	// Check for a send error
	if(rc < 0) {
		printf("send Error: %d %s\n",errno,strerror(errno));
		assert(false);
	}

	INC_STATS(1,msg_sent_cnt,1);
	INC_STATS(1,msg_bytes,size);

}

// Listens to socket for messages from other nodes
uint64_t Transport::recv_msg(base_query * query) {
	int bytes;
	//char * buf = (char *) mem_allocator.alloc(sizeof(char) * MSG_SIZE,get_node_id());
	void * buf;
	bytes = s.recv(&buf, NN_MSG, 0);
	//bytes = s.recv(&buf, NN_MSG, NN_DONTWAIT);

	if(bytes < 0 && errno != 11) {
		printf("Recv Error %d %s\n",errno,strerror(errno));
	}
	// Discard any messages not intended for this node
	//if(bytes <= 0 || GET_RCV_NODE_ID(buf) != get_node_id()) {
	if(bytes <= 0 ) {
		if(errno != 11)
			nn::freemsg(buf);	
		return 0;
	}
	// Queue request for thread to execute
	// Unpack request

	// Calculate time of message delay
	ts_t time;
	memcpy(&time,&((char*)buf)[bytes-sizeof(ts_t)],sizeof(ts_t));
	ts_t time2 = get_sys_clock();
	INC_STATS(1,tport_lat,time2 - time);
#if DEBUG_DISTR
	printf("Msg delay: %d bytes, %f s\n",bytes,((float)(time2-time))/BILLION);
#endif

	ts_t start = get_sys_clock();
	INC_STATS(1,msg_rcv_cnt,1);
	rem_qry_man.unpack(query,buf,bytes);
	nn::freemsg(buf);	
	if(query->dest_id != get_node_id()) {
		INC_STATS(1,rtime_unpack_ndest,get_sys_clock()-start);
		return 0;
	}
	INC_STATS(1,rtime_unpack,get_sys_clock()-start);
	return 1;

}

void Transport::simple_send_msg(int size) {
	void * sbuf = nn_allocmsg(size,0);

	ts_t time = get_sys_clock();
	memcpy(&((char*)sbuf)[0],&time,sizeof(ts_t));

	int rc = s.send(&sbuf,NN_MSG,0);
	if(rc < 0) {
		printf("send Error: %d %s\n",errno,strerror(errno));
		assert(false);
	}
}

uint64_t Transport::simple_recv_msg() {
	int bytes;
	void * buf;
	bytes = s.recv(&buf, NN_MSG, NN_DONTWAIT);
	if(bytes <= 0 ) {
		if(errno != 11)
			nn::freemsg(buf);	
		return 0;
	}

	ts_t time;
	memcpy(&time,&((char*)buf)[0],sizeof(ts_t));
	printf("%d bytes, %f s\n",bytes,((float)(get_sys_clock()-time)) / BILLION);

	nn::freemsg(buf);	
	return bytes;
}

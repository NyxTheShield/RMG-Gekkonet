/******************************************************************************
***N***N**Y***Y**X***X*********************************************************
***NN**N***Y*Y****X*X**********************************************************
***N*N*N****Y******X***********************************************************
***N**NN****Y*****X*X************************ Make Date  : Jul 01, 2007 *******
***N***N****Y****X***X*********************** Last Update: Jul 01, 2007 *******
******************************************************************************/
#pragma once
#include "../common/k_socket.h"
#include "k_instruction.h"
#include "../stats.h"
#include "../errr.h"

#define ICACHESIZE 16

#pragma pack(push, 1)

typedef struct {
    unsigned short serial;
    unsigned short length;
} k_instruction_head;


#pragma pack(pop)

typedef struct {
    k_instruction_head head;
    char * body;
} k_instruction_ptr; 

//void __cdecl kprintf(char * arg_0, ...);

//extern int PACKETLOSSCOUNT;

class k_message : public k_socket {
public:
	    unsigned short      last_sent_instruction;
	    unsigned short      last_processed_instruction;
		unsigned short      last_cached_instruction;
		oslist<k_instruction_ptr, ICACHESIZE> out_cache;
		oslist<k_instruction_ptr, ICACHESIZE> in_cache;
	public:
		int                 default_ipm;
	    k_message(){
	        k_socket();
	        last_sent_instruction = 0;
	        last_processed_instruction = 0;
			last_cached_instruction = -1;
	        default_ipm = 3;
	    }

	    void send_instruction(k_instruction * arg_0){
			char temp[32768];
	        int l = arg_0->write_to_message(temp, 32767);
	        k_message::send(temp, l);
	    }

	    bool send(char * buf, int len){
	        unsigned short vx = last_sent_instruction++;
			k_instruction* ibuf = (k_instruction*)buf;

        if (out_cache.size() == ICACHESIZE-1) {
			free(out_cache[0].body);
			out_cache.removei(0);
        }

		k_instruction_ptr kip;
		kip.body = (char*)malloc(len);
		kip.head.serial = vx;
		kip.head.length = len;
		memcpy(kip.body, buf, len);
		out_cache.add(kip);

        send_message(default_ipm);
        return true;
    }

	    bool send_message(int limit){
	        char buf[0x1000];
	        int len = 1;
	        char * buff = buf + 1;
	        char max_t = std::min((int)out_cache.size(), limit);
			char sent_t = 0;
	        if(max_t > 0) {
	            for (int i = 0; i < max_t; i++) {
	                int cache_index = out_cache.size() - i -1;
	                *(k_instruction_head*)buff = out_cache[cache_index].head;
	                buff += sizeof(k_instruction_head);
	                int l;
					l = out_cache[cache_index].head.length;
					if ((size_t)len + sizeof(k_instruction_head) + (size_t)l > sizeof(buf))
						break;
	                memcpy(buff, out_cache[cache_index].body, l);
	                buff += l;
	                len += l + sizeof(k_instruction_head);
					sent_t++;
	            }
	        }
			buf[0] = sent_t;
			k_socket::send(buf, len);
	        return true;
	    }

    bool receive_instruction(k_instruction * arg_0, bool leave_in_queue, sockaddr_in* arg_8) {
        char var_8000[0x4E20];
        int var_8004 = 0x4E20;
        if (check_recv(var_8000, &var_8004, leave_in_queue, arg_8)) {
			////kprintf("Received:");
            arg_0->read_from_message(var_8000, var_8004);
			//arg_0->to_string();
            return true;
        }
		return false;
    }

	    bool check_recv (char* buf, int * len, bool leave_in_queue, sockaddr_in* addrp){
		/*
		if (in_cache.size() > 0) {
			
			*len = in_cache[0].head.length;
			memcpy(buf, in_cache[0].body, *len);
			//OutputHexx(buf, *len, true);
			
			////kprintf(__FILE__ ":%i", __LINE__);
			
			if(!leave_in_queue) {
				
				////kprintf(__FILE__ ":%i", __LINE__);
				
				last_processed_instruction = in_cache[0].head.serial;
				in_cache.removei(0);
				
			}
			
			return true;
        }*/
		////kprintf(__FILE__ ":%i", __LINE__);
	        if (has_data_waiting) {
			////kprintf(__FILE__ ":%i", __LINE__);
	            char buff      [0x5E20];
	            int  bufflen = 0x5E20;
				sockaddr_in src_addr;
				sockaddr_in* src_addrp = (addrp != NULL) ? addrp : &src_addr;
	            if (k_socket::check_recv(buff, &bufflen, false, src_addrp)) {
					// Ignore spoofed/unexpected packets. Kaillera is not authenticated, so
					// at minimum ensure packets originate from the connected server.
					if (src_addrp->sin_addr.s_addr != addr.sin_addr.s_addr || src_addrp->sin_port != addr.sin_port)
						goto recv_done;
	                unsigned char instruction_count = *buff;

	                char* ptr = buff + 1;
					const char* end = buff + bufflen;
	                if (instruction_count != 0 && ptr < end) {
						
						////kprintf(__FILE__ ":%i", __LINE__);
						
						if ((size_t)(end - ptr) < sizeof(k_instruction_head))
							goto recv_done;
						k_instruction_head latest_head;
						memcpy(&latest_head, ptr, sizeof(latest_head));
						unsigned short latest_serial = latest_head.serial;

						int si = in_cache.size();
						////kprintf("si=%i", si);

						unsigned short tx = latest_serial-last_cached_instruction;
						//kprintf("latest_serial %i -last_cached_instruction %i == tx=%i", latest_serial,last_cached_instruction,tx);

	                    if (tx > 0 && tx < 10 && tx <= ICACHESIZE) {
							if (si < 0 || si > ICACHESIZE) {
								for (int i = 0; i < in_cache.size(); i++) {
									if (in_cache.items[i].body)
										free(in_cache.items[i].body);
									in_cache.items[i].body = NULL;
								}
								in_cache.clear();
								si = 0;
							}
							if (si + (int)tx > ICACHESIZE) {
								// Too much queued. Drop pending data to avoid out-of-bounds.
								for (int i = 0; i < in_cache.size(); i++) {
									if (in_cache.items[i].body)
										free(in_cache.items[i].body);
									in_cache.items[i].body = NULL;
								}
								in_cache.clear();
								si = 0;
							}

	    					in_cache.set_size(si+tx);
							for (int i = si; i < si + (int)tx; i++) {
								in_cache.items[i].head.serial = 0;
								in_cache.items[i].head.length = 0;
								in_cache.items[i].body = NULL;
							}
	    					//kprintf("ss=%i", in_cache.size());
	    
	                        for (int u=0; u<instruction_count; u++) {
								if ((size_t)(end - ptr) < sizeof(k_instruction_head))
									break;
	    						//kprintf(__FILE__ ":%i", __LINE__);
	    
	    						k_instruction_head ih;
								memcpy(&ih, ptr, sizeof(ih));
	    						unsigned short serial = ih.serial;
	                            unsigned short length = ih.length;
	    
	    						ptr += sizeof(k_instruction_head);
	    
	                            if (serial == last_cached_instruction)
	                                break;

								// Validate length and bounds before reading.
								if (length == 0 || length > 0x4E20)
									break;
								if ((size_t)(end - ptr) < (size_t)length)
									break;
	    
	    						unsigned short cix = serial - last_cached_instruction;

								if (cix == 0 || cix > tx) {
									// Unexpected serial delta; skip but keep parsing within bounds.
									ptr += length;
									continue;
								}
	    
	    						PACKETLOSSCOUNT += cix - 1;
	    
	    						int ind = si + (cix) - 1;
	    						
	    						//kprintf("ind=%i", ind);
								if (ind < 0 || ind >= ICACHESIZE) {
									ptr += length;
									continue;
								}
	    
	    						in_cache.items[ind].head.serial = serial;
	    						in_cache.items[ind].head.length = length;
								if (in_cache.items[ind].body)
									free(in_cache.items[ind].body);
								in_cache.items[ind].body = (char*)malloc(length);
								if (!in_cache.items[ind].body)
									break;
	                            memcpy(in_cache.items[ind].body, ptr, length);
	    
	                            ptr += length;
	                        }
	    
	    					last_cached_instruction = latest_serial;

	                    }
	                }
	            }
	        }
recv_done:

	        if (in_cache.size() > 0) {
				if (in_cache.items[0].body == NULL || in_cache.items[0].head.length == 0)
					return false;
				if (*len <= 0)
					return false;
				if ((int)in_cache.items[0].head.length > *len) {
					// Instruction too large for caller's buffer; drop it.
					if (!leave_in_queue) {
						free(in_cache.items[0].body);
						in_cache.items[0].body = NULL;
						in_cache.removei(0);
					}
					return false;
				}

				*len = in_cache[0].head.length;
				memcpy(buf, in_cache[0].body, *len);
				//OutputHexx(buf, *len, true);

				////kprintf(__FILE__ ":%i", __LINE__);

				if(!leave_in_queue) {

					////kprintf(__FILE__ ":%i", __LINE__);

					last_processed_instruction = in_cache[0].head.serial;
					free(in_cache.items[0].body);
					in_cache.items[0].body = NULL;
					in_cache.removei(0);

				}

				return true;
	        }
	        return false;
	    }

    bool has_data(){
        if (in_cache.length == 0)
            return has_data_waiting;
        else
            return true;
    }

    void resend_message(int limit){
        SOCK_SEND_RETR++;
        send_message(limit);
    }
};

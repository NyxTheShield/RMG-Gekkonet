/******************************************************************************
***N***N**Y***Y**X***X*********************************************************
***NN**N***Y*Y****X*X**********************************************************
***N*N*N****Y******X***********************************************************
***N**NN****Y*****X*X************************ Make Date  : Jul 01, 2007 *******
***N***N****Y****X***X*********************** Last Update: Jul 01, 2007 *******
******************************************************************************/
#include "p2p_instruction.h"

#include <algorithm>
#include <cstring>

//void __cdecl kprintf(char * arg_0, ...);

#include <cstdio>

void p2p_instruction::to_string () {
	char * ITYPES[] = {"LOGN", "PING", "PREADY", "TSYNC", "TTIME", "LOAD", "START", "DATA", "DROP", "CHAT", "EXIT"};
	int f = inst.flags;
	int t = inst.type;
	//kprintf("p2p_instruction {%s, %i}", ITYPES[t], f);
}

void p2p_instruction::to_string (char*){
	// Debug helper (currently unused). Intentionally left as a no-op to avoid
	// unsafe writes into unknown buffers.
}

p2p_instruction::p2p_instruction(){
	//memset(&inst, 0, sizeof(inst));
	pos = 0;
	len = 0;
}

p2p_instruction::p2p_instruction(int in, int FLAGS){
	inst.flags = FLAGS;
	inst.type = in;
	pos = 0;
	len = 0;
}

void p2p_instruction::clone(p2p_instruction * arg_0){
	inst = arg_0->inst;
	pos = arg_0->pos;
}

unsigned char p2p_instruction::size(){
	return std::min(255, len+1);
}

void p2p_instruction::store_bytes(const void * arg_0, const int arg_4){
	int fpos = std::min(arg_4 + pos, 255);
	int le = fpos - pos;
	memcpy(&inst.body[pos], arg_0, le);
	pos += le;
}
void p2p_instruction::store_string(const char * arg_0){
	store_bytes(arg_0, 128);
}
void p2p_instruction::store_sstring(const char * arg_0){
	store_bytes(arg_0, 32);
}
void p2p_instruction::store_vstring(const char * arg_0){
	if (arg_0 == NULL) {
		char nul = 0;
		store_bytes(&nul, 1);
		return;
	}

	// vstring is NUL-terminated and capped to 251 bytes (including the NUL).
	int copy_len = 0;
	while (copy_len < 250 && arg_0[copy_len] != 0)
		copy_len++;
	store_bytes(arg_0, copy_len);
	char nul = 0;
	store_bytes(&nul, 1);
}
void p2p_instruction::store_mstring(const char * arg_0){
	store_bytes(arg_0, 64);
}
void p2p_instruction::store_int(const int x){
	store_bytes(&x, 4);
}
void p2p_instruction::store_short(const short x){
	store_bytes(&x, 2);
}
void p2p_instruction::store_char(const char x){
	store_bytes(&x, 1);
}
void p2p_instruction::store_uint(const unsigned int x){
	store_bytes(&x, 4);
}
void p2p_instruction::store_ushort(const unsigned short x){
	store_bytes(&x, 2);
}
void p2p_instruction::store_uchar(const unsigned char x){
	store_bytes(&x, 1);
}

void p2p_instruction::load_bytes(void * arg_0, int arg_4){
	if (pos + arg_4 <= len) {
		memcpy(arg_0, &inst.body[pos], arg_4);
		pos += arg_4;
	}
}
void p2p_instruction::load_string(char * arg_0){
	load_bytes(arg_0, 128);
	arg_0[127] = 0;
}
void p2p_instruction::load_mstring(char * arg_0){
	load_bytes(arg_0, 64);
	arg_0[63] = 0;
}
void p2p_instruction::load_sstring(char * arg_0){
	load_bytes(arg_0, 32);
	arg_0[31] = 0;
}
void p2p_instruction::load_vstring(char* arg_0, unsigned int arg_4) {
	if (arg_0 == NULL || arg_4 == 0) {
		// Skip the string to keep parsing aligned.
		unsigned int remaining = (pos <= len) ? (unsigned int)(len - pos) : 0;
		unsigned int nul_pos = 0;
		while (nul_pos < remaining && inst.body[pos + nul_pos] != 0)
			nul_pos++;
		unsigned int consume = (nul_pos < remaining) ? (nul_pos + 1) : remaining;
		pos = (unsigned char)std::min((unsigned int)len, (unsigned int)pos + consume);
		return;
	}

	unsigned int remaining = (pos <= len) ? (unsigned int)(len - pos) : 0;
	unsigned int nul_pos = 0;
	while (nul_pos < remaining && inst.body[pos + nul_pos] != 0)
		nul_pos++;

	unsigned int consume = (nul_pos < remaining) ? (nul_pos + 1) : remaining;
	unsigned int max_copy = arg_4 - 1;
	unsigned int to_copy = std::min(max_copy, consume);

	if (to_copy > 0)
		memcpy(arg_0, &inst.body[pos], to_copy);
	arg_0[std::min(to_copy, max_copy)] = 0;

	pos = (unsigned char)std::min((unsigned int)len, (unsigned int)pos + consume);
}
int p2p_instruction::load_int(){
	int x;
	load_bytes(&x,4);
	return x;
}
char p2p_instruction::load_char(){
	char x;
	load_bytes(&x,1);
	return x;
}
short p2p_instruction::load_short(){
	short x;
	load_bytes(&x,2);
	return x;
}
unsigned int p2p_instruction::load_uint(){
	unsigned int x;
	load_bytes(&x,4);
	return x;
}
unsigned char p2p_instruction::load_uchar(){
	unsigned char x;
	load_bytes(&x,1);
	return x;
}
unsigned short p2p_instruction::load_ushort(){
	unsigned short x;
	load_bytes(&x,2);
	return x;
}

int p2p_instruction::write_to_message(char * arg_0){
	*arg_0++ = *((char*)&inst);
	memcpy(arg_0, &inst.body, pos);
	return 1 + pos;
}

void p2p_instruction::read_from_message(char * p_buffer, int p_buffer_len){
	if (p_buffer == NULL || p_buffer_len <= 0) {
		memset(&inst, 0, sizeof(inst));
		pos = 0;
		len = 0;
		return;
	}

	*((char*)&inst) = *p_buffer++;
	int payload_len = p_buffer_len - 1;
	if (payload_len < 0)
		payload_len = 0;
	if (payload_len > 255)
		payload_len = 255;
	len = (unsigned char)payload_len;
	if (payload_len > 0)
		memcpy(&inst.body, p_buffer, payload_len);
	pos = 0;
}

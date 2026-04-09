#pragma once

#include <stddef.h>
#include <string.h>

static const char kP2PAppCodePrefix[] = " {CC:";
static const char kP2PAppCodeSuffix[] = "}";

inline bool p2p_appcode_split_inplace(char* app, char* out_code, size_t out_code_len) {
	if (app == NULL || *app == 0)
		return false;

	char* prefix = strstr(app, kP2PAppCodePrefix);
	if (prefix == NULL)
		return false;

	char* code_start = prefix + strlen(kP2PAppCodePrefix);
	char* suffix = strstr(code_start, kP2PAppCodeSuffix);
	if (suffix == NULL)
		return false;

	if (out_code != NULL && out_code_len > 0) {
		size_t code_len = (size_t)(suffix - code_start);
		if (code_len >= out_code_len)
			code_len = out_code_len - 1;
		memcpy(out_code, code_start, code_len);
		out_code[code_len] = 0;
	}

	*prefix = 0;
	*suffix = 0;
	return true;
}

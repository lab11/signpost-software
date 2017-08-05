#pragma once

//performs get, following redirects and all, until it gets a final status code
//returns the status code
//or a cellular error code
int signpost_http_get(const char* url);

//performs post, following redirects, until it gets a final status code response
//it uses application/octetstream as the content-type
//returns the status code
//or returns an error
int signpost_http_post(const char* url, uint8_t* buf, size_t buf_len);

//return chunks of the body of the response to the last request
//returns 0 when body is done
int signpost_get_http_body(size_t offset, uint8_t* buf, size_t buf_len);

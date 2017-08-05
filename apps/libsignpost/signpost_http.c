#include "tock.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "http_parser.h"
#include "sara_u260.h"
#include "signpost_http.h"

//performs get, following redirects and all, until it gets a final status code
//returns the status code
//or a cellular error code

#define VALUE 0
#define FIELD 1
static uint8_t last_cb = -1;
static bool is_location = false;
static bool location_valid = false;

#define URL_LEN 150
static char next_url[URL_LEN];
static int parser_header_value(__attribute__ ((unused)) http_parser* parser, const char* at, size_t length) {
    //if the location header was found, save the next value
    static uint8_t field_offset;
    if(is_location) {
        if(last_cb != FIELD) {
            //copy the field name into the field name buffer at offset
            if(field_offset + length < URL_LEN) {
                memcpy(next_url + field_offset, at, length);
                field_offset += length;
            } else {
                //this is too long - forget it
                field_offset += length;
                location_valid = false;
            }
        } else {
            //start over
            location_valid = true;
            field_offset = 0;
            if(field_offset + length < URL_LEN) {
                memcpy(next_url + field_offset, at, length);
                field_offset += length;
            } else {
                //this is too long - forget it
                field_offset += length;
                location_valid = false;
            }
        }
    }

    return 0;
}

static int parser_header_field(__attribute__ ((unused)) http_parser* parser, const char* at, size_t length) {
    static char field[20];
    static uint8_t field_offset;
    if(last_cb != VALUE) {
        //copy the field name into the field name buffer at offset
        if(field_offset + length < 20) {
            memcpy(field + field_offset, at, length);
            field_offset += length;
        } else {
            //this is too long - forget it
            field_offset += length;
        }
    } else {
        //start over
        is_location = false;
        field_offset = 0;
        if(field_offset + length < 20) {
            memcpy(field + field_offset, at, length);
            field_offset += length;
        } else {
            //this is too long - forget it
            field_offset += length;
        }
    }

    if(!strncasecmp(field,"location",20)) {
        //this was a location field - set the flag
        is_location = true;
    }

    return 0;
}

static bool done_parsing;
static int status;
static int parser_header_complete(http_parser* parser) {
    //now that the header is complete set the done flag and the status code
    status = parser->status_code;
    done_parsing = true;
    return 0;
}

int signpost_http_get(const char* url) {
    //parse the url
    struct http_parser_url* u = malloc(sizeof(struct http_parser_url));
    if(!u) {
        return TOCK_ENOMEM;
    }

    http_parser_parse_url(url, strlen(url), 0, u);

    //get the host and path offsets
    char host[URL_LEN];
    char path[URL_LEN];
    snprintf(host, URL_LEN,"%.*s",u->field_data[UF_HOST].len,url+u->field_data[UF_HOST].off);
    snprintf(path, URL_LEN,"%.*s",u->field_data[UF_PATH].len,url+u->field_data[UF_PATH].off);
    free(u);

    int ret = sara_u260_basic_http_get(host, path);
    if(ret < SARA_U260_SUCCESS) {
        return ret;
    }

    static uint8_t redirects = 0;

    while(redirects < 2) {
        //create a parser and start parsing the response
        http_parser_settings settings;
        settings.on_headers_complete = parser_header_complete;
        settings.on_header_field = parser_header_field;
        settings.on_header_value = parser_header_value;

        http_parser* parser;
        parser = malloc(sizeof(http_parser));
        if(!parser) {
            return TOCK_ENOMEM;
        }

        done_parsing = false;
        while(!done_parsing) {
            const char read_buf[200];
            static size_t offset = 0;
            ret = sara_u260_get_get_partial_response((uint8_t*)read_buf, offset, 200);
            if(ret < SARA_U260_SUCCESS) {
                free(parser);
                return ret;
            }

            ret = http_parser_execute(parser, &settings, read_buf, ret);
        }

        free(parser);
        if(status == 301 || status == 302 || status == 307 || status == 308) {
            //we need to follow the redirect
            //
            //free the parser - we will create a new one

            //was there a location url?
            if(location_valid) {
                //parse the url for a post
                u = malloc(sizeof(struct http_parser_url));
                if(!u) {
                    return TOCK_ENOMEM;
                }

                http_parser_parse_url(next_url, strnlen(next_url, URL_LEN), 0, u);

                //get the host and path offsets
                char rhost[URL_LEN];
                char rpath[URL_LEN];
                snprintf(rhost, URL_LEN,"%.*s",u->field_data[UF_HOST].len,url+u->field_data[UF_HOST].off);
                snprintf(rpath, URL_LEN,"%.*s",u->field_data[UF_PATH].len,url+u->field_data[UF_PATH].off);
                free(u);

                ret = sara_u260_basic_http_get(rhost, rpath);
                if(ret < SARA_U260_SUCCESS) {
                    return ret;
                }

                redirects++;
            } else {
                return TOCK_FAIL;
            }
        } else {
            break;
        }
    }

    if(redirects == 2) {
        //too many redirects - abort
        //note the parser must have been freed to get here
        return TOCK_FAIL;
    }

    //okay now we have the status code - return
    //we will restart parsing if the client wants the results
    return status;
}

//performs post, following redirects, until it gets a final status code response
//it uses application/octetstream as the content-type
//returns the status code
//or returns an error
int signpost_http_post(const char* url, uint8_t* buf, size_t buf_len) {
    //parse the url
    struct http_parser_url* u = malloc(sizeof(struct http_parser_url));
    if(!u) {
        return TOCK_ENOMEM;
    }

    http_parser_parse_url(url, strlen(url), 0, u);

    //get the host and path offsets
    char host[URL_LEN];
    char path[URL_LEN];
    snprintf(host, URL_LEN,"%.*s",u->field_data[UF_HOST].len,url+u->field_data[UF_HOST].off);
    snprintf(path, URL_LEN,"%.*s",u->field_data[UF_PATH].len,url+u->field_data[UF_PATH].off);
    free(u);

    int ret = sara_u260_basic_http_post(host, path, buf, buf_len);
    if(ret < SARA_U260_SUCCESS) {
        return ret;
    }

    static uint8_t redirects = 0;

    while(redirects < 2) {
        //create a parser and start parsing the response
        http_parser_settings settings;
        settings.on_headers_complete = parser_header_complete;
        settings.on_header_field = parser_header_field;
        settings.on_header_value = parser_header_value;

        http_parser* parser;
        parser = malloc(sizeof(http_parser));
        if(!parser) {
            return TOCK_ENOMEM;
        }

        http_parser_init(parser, HTTP_RESPONSE);

        done_parsing = false;
        while(!done_parsing) {
            const char read_buf[200];
            static size_t offset = 0;
            ret = sara_u260_get_get_partial_response((uint8_t*)read_buf, offset, 200);
            if(ret < SARA_U260_SUCCESS) {
                free(parser);
                return ret;
            }

            ret = http_parser_execute(parser, &settings, read_buf, ret);
        }
        free(parser);

        if(status == 301 || status == 302 || status == 307 || status == 308) {
            //we need to follow the redirect
            //
            //free the parser - we will create a new one

            //was there a location url?
            if(location_valid) {
                //parse the url for a post
                u = malloc(sizeof(struct http_parser_url));
                if(!u) {
                    return TOCK_ENOMEM;
                }

                http_parser_parse_url(next_url, strlen(next_url), 0, u);

                //get the host and path offsets
                char rhost[URL_LEN];
                char rpath[URL_LEN];
                snprintf(rhost, URL_LEN,"%.*s",u->field_data[UF_HOST].len,url+u->field_data[UF_HOST].off);
                snprintf(rpath, URL_LEN,"%.*s",u->field_data[UF_PATH].len,url+u->field_data[UF_PATH].off);
                free(u);

                ret = sara_u260_basic_http_post(rhost, rpath, buf, buf_len);
                if(ret < SARA_U260_SUCCESS) {
                    return ret;
                }

                redirects++;
            } else {
                return TOCK_FAIL;
            }
        } else {
            break;
        }
    }

    if(redirects == 2) {
        //too many redirects - abort
        return TOCK_FAIL;
    }

    //okay now we have the status code - return
    //we will restart parsing if the client wants the results
    return status;
}

//return chunks of the body of the response to the last request
//returns 0 when body is done
int signpost_get_http_body(__attribute__ ((unused)) size_t offset,
                            __attribute__ ((unused)) uint8_t* buf,
                            __attribute__ ((unused)) size_t buf_len) {
    return 0;
}

// Jsonrpc (client) implementation header
// Copyright (C) 2022 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
// See: https://www.jsonrpc.org/specification

#ifndef JSONRPC_H
#define JSONRPC_H

#include <string>
#include <iostream>
#include <unistd.h>
#include "config.h"
#include "stdint.h"
#if HAVE_JSON_C
    #include <json-c/json.h>
#else
    struct json_object;
    enum json_type {json_type_null, json_type_string};
    #define JSON_C_TO_STRING_SPACED 1
    json_object* json_tokener_parse(const char*);
    json_object* json_object_new_object();
    json_object* json_object_new_string(const char*);
    json_object* json_object_new_int(int32_t);
    json_object* json_object_new_boolean(bool);
    json_object* json_object_new_double(double);
    const char*  json_object_get_string(json_object*);
    int32_t      json_object_get_int(json_object*);
    bool         json_object_get_boolean(json_object*);
    uint64_t     json_object_get_uint64(json_object*);
    double       json_object_get_double(json_object*);
    bool         json_object_object_get_ex (json_object*,const char*, json_object**);
    json_type    json_object_get_type (json_object*);
    int          json_object_object_add(json_object*, const char*, json_object*);
    size_t       json_object_array_length(json_object*);
    json_object* json_object_array_get_idx(json_object*, size_t);
    const char*  json_object_to_json_string_length(json_object*, int, size_t*);
    const char*  json_object_to_json_string(json_object*);
    json_object* json_object_new_array();
    void         json_object_array_add(json_object*, json_object*);
#endif
using namespace std;

static struct
{
    int ParseError = -32700;
    int InvalidRequest = -32600;
    int MethodNotFound = -32601;
    int InvalidParams = -32602;
    int InternalError = -32603;
    int ServerNotInitialized = -32002;
    int UnknownErrorCode = -32001;
} LSPErrCode;

struct jsonrpc_error : public runtime_error
{
private:
    json_object *error;

public:
    jsonrpc_error(int code, string message, json_object *data = NULL) : runtime_error(to_string(code) + "-" + message)
    {
        error = json_object_new_object();
        json_object_object_add(error, "code", json_object_new_int(code));
        json_object_object_add(error, "message", json_object_new_string(message.c_str()));
        if (data)
            json_object_object_add(error, "data", data);
    }

    json_object *get_json() { return error; }
};

struct jsonrpc_header
{
    size_t content_length;
    string content_type;

    jsonrpc_header() : content_length{0}, content_type{"application/vscode-jsonrpc; charset=utf-8"}
    {}
};

struct jsonrpc_request
{
    string jsonrpc;
    string method;
    json_object *params;
    int id;
    bool id_is_string; // true iff the original id was sent as "id"

    bool is_notification()
    {
        return id == -1;
    }

    jsonrpc_request(char *request_string)
    {
        json_object *request = json_tokener_parse(request_string);
        json_object* value;
        if (request)
        {
            if(json_object_object_get_ex(request, "jsonrpc", &value))
                jsonrpc = json_object_get_string(value);
            if(json_object_object_get_ex(request, "method", &value))
                method  = json_object_get_string(value);
            if(json_object_object_get_ex(request, "params", &value))
                params = value;
            json_object *jid;
            bool has_jid = json_object_object_get_ex(request, "id", &jid);
            if (!has_jid || json_object_get_type(jid) == json_type_null)
                id = -1;
            else if (json_object_get_type(jid) == json_type_string){
                id = atoi(json_object_get_string(jid));
                id_is_string = true;
            }
            else{
                id = json_object_get_int(jid);
                id_is_string = false;
            }
        }

        if (!request || jsonrpc != "2.0")
            throw jsonrpc_error(LSPErrCode.InvalidRequest, "");
    }
};

struct jsonrpc_response
{
private:
    json_object *payload;

public:
    bool result_or_error_set;

    jsonrpc_response()
    {
        payload = json_object_new_object();
        json_object_object_add(payload, "jsonrpc", json_object_new_string("2.0"));
        result_or_error_set = false;
    }

    bool set_error(json_object *e)
    {
        if (result_or_error_set)
            return false;
        json_object_object_add(payload, "error", e);
        result_or_error_set = true;
        return true;
    }

    bool set_result(json_object *r)
    {
        if (result_or_error_set)
            return false;
        json_object_object_add(payload, "result", r);
        result_or_error_set = true;
        return true;
    }

    json_object *to_json(jsonrpc_request *req)
    {
        if (!result_or_error_set)
            throw jsonrpc_error(LSPErrCode.InternalError, "Either result or error must be set on a json response");
        // If there was an error in detecting the id in the Request object (e.g. Parse error/Invalid Request), it MUST be Null.
        if (req->id_is_string)
        {
            string id = to_string(req->id);
            json_object_object_add(payload, "id", req->id >= 0 ? json_object_new_string(id.c_str()) : NULL);
        }else{
            int id = req->id;
            json_object_object_add(payload, "id", req->id >= 0 ? json_object_new_int(id) : NULL);
        }
        return payload;
    }
};

class jsonrpc_connection
{
private:
    int IN_FILNO;
    int OUT_FILENO;
    int verbose;

    bool _read_header_line(string &line);
    void _read_header(jsonrpc_header &h);
    void _write_header_line(string field, string value, bool final_line = false);

public:
    jsonrpc_connection(int in_fd, int out_fd, int verbose) : IN_FILNO{in_fd}, OUT_FILENO{out_fd}, verbose{verbose}
    {
        // The in file-descriptor is a terminal
        if(1 == isatty(IN_FILNO)){
            cerr << "Warning: The language server should not be started in a terminal.\n" \
                 << "It should be run as the child of a LSP client" << endl;
            exit(1);
        }
    }

    // Client functions
    void wait_for_request();
    jsonrpc_request *get_request();
    void send_response(jsonrpc_request *request, jsonrpc_response *response);
    // TODO: Server functions send request/notification (needed only for completeness, not required)
};

#endif

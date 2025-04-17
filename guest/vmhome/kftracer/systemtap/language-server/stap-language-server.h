// Language server header.
// Copyright (C) 2022 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
// See: https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/

#ifndef LANGUAGE_SERVER_H
#define LANGUAGE_SERVER_H

#include "jsonrpc.h"
#include "session.h"
#include "parse.h"
#include "staptree.h"

#include <string>
#include <ostream>
#include <sstream>
#include <map>
#include <vector>
#include <functional>
#include <list>
#include <unordered_map>

using namespace std;

enum TextDocumentSyncKind
{
    None = 0,        // Documents should not be synced at all
    Full = 1,        // Documents are synced by always sending the full content of the document
    Incremental = 2  // Documents are synced by sending incremental updates
};

class lsp_object
{
private:
    json_object *j_obj; // The internal json storage

    json_object *_to_json_object(string value)       { return json_object_new_string(value.c_str()); }
    json_object *_to_json_object(const char *value)  { return json_object_new_string(value); }
    json_object *_to_json_object(int value)          { return json_object_new_int(value); }
    json_object *_to_json_object(bool value)         { return json_object_new_boolean(value); }
    json_object *_to_json_object(double value)       { return json_object_new_double(value); }
    json_object *_to_json_object(lsp_object value)   { return value.to_json(); }

    json_object *_get_and_check(string key)
    {
        json_object *value;
        if (!json_object_object_get_ex(j_obj, key.c_str(), &value))
            throw jsonrpc_error(LSPErrCode.InternalError, "Keyerror: " + key + " not found");
        return value;
    }

public:
    lsp_object(json_object *j_obj) : j_obj{j_obj}
    {}

    lsp_object() : j_obj{json_object_new_object()}
    {}

    json_object *to_json() { return j_obj; }

    const char  *extract_string(string key) { return json_object_get_string(_get_and_check(key)); }
    int          extract_int(string key)    { return json_object_get_int(_get_and_check(key)); }
    bool         extract_bool(string key)   { return json_object_get_boolean(_get_and_check(key)); }
    double       extract_double(string key) { return json_object_get_double(_get_and_check(key)); }

    template <typename T>
    vector<T> extract_array(string key)
    {
        json_object *arr = _get_and_check(key);
        vector<T> result;
        for (size_t i = 0; i < json_object_array_length(arr); i++)
            result.push_back(json_object_array_get_idx(arr, i));
        return result;
    }

    lsp_object extract_substruct(string key)
    {
        json_object *sub_struct = _get_and_check(key);
        return lsp_object(sub_struct);
    }

    void insert(string key, const char *value)  { json_object_object_add(j_obj, key.c_str(), _to_json_object(value)); }
    void insert(string key, int value)          { json_object_object_add(j_obj, key.c_str(), _to_json_object(value)); }
    void insert(string key, bool value)         { json_object_object_add(j_obj, key.c_str(), _to_json_object(value)); }
    void insert(string key, double value)       { json_object_object_add(j_obj, key.c_str(), _to_json_object(value)); }
    void insert(string key, string &value)      { json_object_object_add(j_obj, key.c_str(), _to_json_object(value.c_str())); }
    void insert(string key, lsp_object &value)  { json_object_object_add(j_obj, key.c_str(), _to_json_object(value)); }

    template <typename T>
    void insert(string key, vector<T> values)
    {
        json_object *arr = json_object_new_array();
        for (auto v = values.begin(); v != values.end(); ++v)
            json_object_array_add(arr, _to_json_object(*v));
        json_object_object_add(j_obj, key.c_str(), arr);
    }

    lsp_object insert(string key)
    {
        // Calling insert without a value will create a substructure named key if one does already not exist
        // Otherwise it will return the existing structure
        // This allows for syntax like `foo.insert("bar").insert("baz", 42))` -> {"bar": {"baz": 42}}
        json_object *sub_struct;
        if (json_object_object_get_ex(j_obj, key.c_str(), &sub_struct))
            return lsp_object(sub_struct);
        lsp_object obj;
        json_object_object_add(j_obj, key.c_str(), _to_json_object(obj));
        return obj;
    }

    bool contains(string key) { return json_object_object_get_ex(j_obj, key.c_str(), NULL); }
};

string
join_vector(vector<string> vec, string delim);

int
pass_1(systemtap_session &s, string &code);

int passes_0_4(systemtap_session &s);

class document
{
private:
    string uri;
    string source;
    systemtap_session* s;

public:
    // globals/functions defined within the document, which may be refrenced (ex. completion)
    vector<vardecl*> globals;
    map<string,functiondecl*> functions;

    document(string uri, systemtap_session* s, string source = "") : uri{uri}, source{source}, s{s}
    {
        register_code_blocks(); // Parse the given source and register all local definitions
    }

    vector<string> get_lines();

    // Get the last code_block before last_line
    // Return the start line number of the code_block
    int get_last_code_block(vector<string>& lines, string& code, int last_line);

    void register_code_block(string& code, int first_line);

    // Register the codeblocks from the end of the source (or if specified last_line) to start_line
    // Optionally clear the older function/global declarations
    void register_code_blocks(int start_line = 0, int last_line = -1, bool clear_decls = true);

    void apply_change(lsp_object change, TextDocumentSyncKind kind);
};

class workspace
{
private:
    map<string, document*> docs;
    systemtap_session* s;

    document *create_document(string doc_uri, string source = "")
    {
        return new document(doc_uri, s, source);
    }

public:
    workspace(systemtap_session* s) : s{s} {}

    document *get_document(string doc_uri)
    {
        // Get the document, or create one if none are found
        auto d = docs.find(doc_uri);

        if (d == docs.end())
        {
            document *doc = create_document(doc_uri);
            docs.insert({doc_uri, doc});
            return doc;
        }
        return d->second;
    }

    void add_document(lsp_object text_document)
    {
        string doc_uri = text_document.extract_string("uri");
        docs.insert({doc_uri, create_document(doc_uri, text_document.extract_string("text"))});
    }

    void remove_document(string doc_uri)
    {
        delete get_document(doc_uri);
        docs.erase(doc_uri);
    }

    void update_document(lsp_object text_doc, lsp_object change, TextDocumentSyncKind kind)
    {
        string doc_uri = text_doc.extract_string("uri");
        get_document(doc_uri)->apply_change(change, kind);
    }
};

class language_server
{
private:
    map<string, function<jsonrpc_response *(language_server *ls, json_object *p)>> registered_lsp_methods;
    jsonrpc_response *handle_lsp_method(jsonrpc_request *req);
    
    void register_lsp_method(string method, function<jsonrpc_response *(language_server *ls, json_object *p)> handler)
    { registered_lsp_methods.insert({method, handler}); }

public:
    language_server(systemtap_session *s, unsigned int verbose);
    int run();

    systemtap_session *s;
    unsigned int verbose;
    workspace *wspace;
    bool running;
    bool init_request_received;
    bool shutdown_request_received;
    // The state once the parser is complete, which is then used for completion
    shared_ptr<parser_completion_state> c_state;

    bool is_registered(string method) { return registered_lsp_methods.find(method) != registered_lsp_methods.end(); }

    // Throws jsonrpc_error if condition is false 
    void is_method_undefined(jsonrpc_request *req);
    void is_premature_request(jsonrpc_request *req);
    void is_already_shutdown(jsonrpc_request *req);
    void is_duplicate_initialization(jsonrpc_request *req);
};

class lsp_method
{
protected:
    language_server *lang_server;

public:
    lsp_method(language_server *lang_server) : lang_server{lang_server}
    {}

    virtual jsonrpc_response *handle(json_object *)
    { return nullptr; }

    template <typename T>
    static jsonrpc_response *handler(language_server *lang_server, json_object *params)
    {
        T t(lang_server);
        return t.handle(params);
    }
};

class lsp_method_text_document_completion : public lsp_method
{
private:
    vector<lsp_object> completion_items;
    lsp_object insert_position;
    document *doc;

    // If applicable (in a probe body), contains the <var_name, var_type> pairs
    vector<pair<string, string>> target_variables;

    // Helper methods
    void add_completion_item(string text, string type = "", string docs = "", string insert_text = "");
    void complete_body();
    void complete_probe_signature();
    void complete_path(string path);
    void complete_string();
    void complete(string code);

public:
    lsp_method_text_document_completion(language_server *lang_server) : lsp_method(lang_server) {}
    static constexpr const char* TEXT_DOCUMENT_COMPLETION = "textDocument/completion";
    jsonrpc_response *handle(json_object *p);
};

#endif

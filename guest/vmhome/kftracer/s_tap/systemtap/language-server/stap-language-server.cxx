// Systemtap language server
// Copyright (C) 2022 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
#include "stap-language-server.h"
#include "jsonrpc.h"
#include "config.h"
#include "parse.h"
#include "session.h"
#include "tapsets.h"
#include "staputil.h"
#include <ext/stdio_filebuf.h>

using namespace std;

static void
transcribe_functions_and_globals(systemtap_session &s)
{
    for (auto f = s.library_files.begin(); f != s.library_files.end(); ++f)
    {
        for (auto it = (*f)->functions.begin(); it != (*f)->functions.end(); ++it)
        {
            functiondecl *v = *it;
            if (v->synthetic || !startswith(v->name, "__global_"))
                continue;
            s.functions[v->name] = v;
        }

        for (auto it = (*f)->globals.begin(); it != (*f)->globals.end(); ++it)
        {
            vardecl *v = *it;
            if (v->synthetic || !startswith(v->name, "__global_"))
                continue;
            s.globals.push_back(v);
        }
    }
}

int
pass_1(systemtap_session &s, string &code)
{
    // Set up session to only run pass 1.
    s.last_pass = 1;
    s.monitor = false;
    s.seen_errors.clear(); // The pass is standalone
    s.warningerr_count = 0;

    // Add the code snippet
    s.cmdline_script = code;
    s.have_script = true;

    // Ignore the output since it will probably raise a parse error
    cout.setstate(ios_base::failbit);
    cerr.setstate(ios_base::failbit);
    int rc = 0;
    try
    {
        rc = passes_0_4(s);
    }
    catch (const parse_error &pe)
    {
        rc = 1;
    }
    // Restore
    cout.clear();
    cerr.clear();
    return rc;
}

string
join_vector(vector<string> vec, string delim)
{
    stringstream result;
    for (auto it = vec.begin(); it != vec.end(); ++it)
    {
        result << *it;
        if (it != vec.end() - 1)
            result << delim;
    }
    return result.str();
}

vector<string>
extract_lines(string s){
    vector<string> lines;
    stringstream ss = stringstream(s);
    for (string line; getline(ss, line, '\n');)
        lines.push_back(line);
    return lines;
}

vector<string>
document::get_lines(){
    return extract_lines(source);
}

int
document::get_last_code_block(vector<string>& lines, string& code, int last_line){
    static vector<string> start_tokens = {"probe", "function", "global", "private"};

    // NB: For the right-strip, DON'T remove spaces since token seperation requires it ex. "probe" vs "probe "
    static auto strip = [](string s)
        { return s.erase(0, s.find_first_not_of("\t\n\r ")).erase(s.find_last_not_of("\t\n\r") + 1); };

    // Considering the first num_lines, return true
    // iff the num_lines+1st line is in its own scope
    auto should_stop_here = [this](size_t num_lines){
        size_t n = source.size();
        size_t ln_num = 0;
        int scope = 0;
        for(size_t c = 0; c < n && ln_num < num_lines; c++){
            if('\n' == source[c]){
                ln_num++;
                continue;
            }
            if('{' == source[c]){
                scope++;    
            }else if('}' == source[c]){
                scope--;
                // We have more closing than opening, so this is not in an above scope
                // Important to exit here, since '}}}{{{' would result in scope == 0,
                // so exit the second we have a negative scope
                if(scope < 0) return true;
            }
        }
        return scope <= 0; // For every opening, there was as least 1 close
    };

    vector<string> block;
    int i = last_line;
    // Keep appending lines until a valid start token (or at least the start of one) is found.
    for (; i >= 0; i--)
    {
        string line = strip(lines[i]);
        block.push_back(line); // The last step will be to reverse the whole vector and merge it
        vector<string> words;
        tokenize(line, words, " ");
        if (words.size() > 0 && any_of(start_tokens.cbegin(), start_tokens.cend(), [words](string s_token)
                                    { return startswith(s_token, words[0]); }))
        {
            // Before saying we've just found the line, look up a little and make
            // sure we're not just within a larger structure. Ex.
            // probe oneshot {
            //     pri
            // shouldn't complete to private, but intead should complete as within a body (ex. print)
            if(should_stop_here(i)) break;
        }
    }
    reverse(block.begin(), block.end());
    code = join_vector(block, "\n");
    return i;
}

void
document::register_code_block(string& code, int first_line){
    // Clear old user_files, since each registeration should be independant
    for(auto uf = s->user_files.cbegin(); uf != s->user_files.cend(); uf = s->user_files.erase(uf) )
        delete *uf;

    // Parse the code_block
    int rc = pass_1(*s, code);

    // Location is made relative to the document start.
    // This is needed since in order to determine the current valid definitions,
    // we need to see the absolute line numbers on which they were created
    auto update_location = [first_line](auto sym){
        source_loc new_loc(sym->tok->location);
        new_loc.line += first_line - 1;
        sym->tok = sym->tok->adjust_location(new_loc);
        return sym;
    };

    if(0 == rc){ // Just skip if there is an error in a code blocks
        stapfile *user_file = s->user_files.front();
        for(auto g = user_file->globals.begin(); g != user_file->globals.end(); ++g)
            globals.push_back(update_location(*g));
        for(auto f = user_file->functions.begin(); f != user_file->functions.end(); ++f)
            functions.insert({ (*f)->name, update_location(*f) });
        // TODO: Probe aliases (user_file->aliases)
    }
}

void 
document::register_code_blocks(int start_line, int last_line, bool clear_decls){
    if(clear_decls)
    {
        for(auto g = globals.cbegin(); g != globals.cend(); g = globals.erase(g) )
            delete *g;
        for(auto f = functions.cbegin(); f != functions.cend(); f = functions.erase(f) )
            delete f->second;
    }

    vector<string> lines = get_lines();
    if(0 == lines.size()) return; // Nothing to register
    string code;
    // If the page is ever shortened, the last affected line could fall outside of the doc length
    last_line = min(last_line, int(lines.size()));
    int line_num = (last_line != -1) ? last_line : lines.size();
    // NB: When registering the code blocks we want to parse as normal
    // not exit early when we find an error (as we would for completion).
    // So we temporarily disable language_server_mode
    s->language_server_mode = false;
    while(start_line <= (line_num = get_last_code_block(lines, code, line_num-1)))
        register_code_block(code, line_num);
    s->language_server_mode = true;
}

void
document::apply_change(lsp_object change, TextDocumentSyncKind kind){
    string text = change.extract_string("text");

    // Count the number of occurences of c in str
    auto count = [](string str, char c){
        int n = 0;
        for(size_t i = 0; i < str.size(); i++)
            if (str[i] == c) n++;
        return n;
    };

    // Finds the line and character of the first difference between two
    // vectors of strings
    auto first_diff = [](vector<string>& old_lines, vector<string>& new_lines){
        int first_ln, first_col;
        int line_count = min(old_lines.size(), new_lines.size());
        if(0 == line_count){
            first_ln = first_col = 0;
        }else{
            // Find the first differing line
            for(first_ln = 0; first_ln < line_count; first_ln++)
                if(old_lines[first_ln] != new_lines[first_ln]) break;
            
            if(first_ln == line_count){
                // Appending to the document
                first_ln = line_count - 1;
                first_col = old_lines[first_ln].size();
            }else{
                int line_len = min(old_lines[first_ln].size(), new_lines[first_ln].size());
                // Find the first different character on first_ln
                for(first_col = 0; first_col < line_len; first_col++)
                    if(old_lines[first_ln][first_col] != new_lines[first_ln][first_col]) break;
            }
        }
        lsp_object pos;
        pos.insert("line", first_ln);
        pos.insert("character", first_col);
        return pos;
    };

    auto safe_substr = [](string s, size_t __pos, size_t __n = string::npos){ return s.substr(min(__pos, s.size()), __n); };

    // Finds the line and character of the last difference between two
    // vectors of strings as a NEGATIVE OFFSET from the end
    auto last_diff = [safe_substr](vector<string> old_lines, vector<string> new_lines, lsp_object first_difference){
        int last_ln, last_col;
        size_t old_lines_n = old_lines.size(), new_lines_n = new_lines.size();

        int line_count = min(old_lines_n, new_lines_n);
        if(0 == line_count){
            last_ln = last_col = 0;
        }else{
            // Find the last differing line
            for(last_ln = -1; last_ln > -line_count; last_ln--)
                if(old_lines[old_lines_n + last_ln] != new_lines[new_lines_n + last_ln]) break;

            string old_line, new_line;
            if(last_ln <= -line_count){
                last_ln = -line_count;
                old_line = safe_substr(old_lines[ old_lines_n + last_ln ], first_difference.extract_int("character"));
                new_line = safe_substr(new_lines[ new_lines_n + last_ln ], first_difference.extract_int("character"));
            }else{
                old_line = old_lines[ old_lines_n + last_ln ];
                new_line = new_lines[ new_lines_n + last_ln ];
            }

            size_t old_line_n = old_line.size(), new_line_n = new_line.size();
            int line_len = min(old_line_n, new_line_n);
            for(last_col = -1; last_col >= -line_len ; last_col--)
                if(old_lines[ old_lines_n + last_ln][ old_line_n + last_col] != new_lines[ new_lines_n + last_ln][new_line_n + last_col]) break;
        }
        lsp_object pos;
        pos.insert("line", last_ln);
        pos.insert("character", last_col);
        return pos;
    };

    // Get the substring from within the old_lines which is the actuall delta between old and new
    auto extract_text = [safe_substr](vector<string>& new_lines, lsp_object& start, lsp_object& end){
        size_t start_ln  = start.extract_int("line");
        size_t start_col = start.extract_int("character");
        size_t end_ln    = end.extract_int("line");
        size_t end_col   = end.extract_int("character");
        if(start_ln == new_lines.size() + end_ln){
            if(0 == end_ln) return string();
            int length =  new_lines[start_ln].size() + end_col - start_col + 1;
            return safe_substr(new_lines[start_ln], start_col, length);
        }

        string result = safe_substr(new_lines[start_ln], start_col) + "\n";
        for(auto line_it = new_lines.begin()+start_ln+1; line_it != new_lines.begin()+new_lines.size()+end_ln+1; ++line_it)
            result += *line_it + "\n";
        if(0 != end_ln){
            int length = new_lines[new_lines.size() + end_ln].size() + end_col + 1;
            result += safe_substr(new_lines[new_lines.size() + end_ln], 0, length);
        }
        return result;
    };
    
    switch (kind)
    {
    case None:
        break; // Do Nothing
    case Full:
    {
        // If the source is empty then we just apply the new full change as expected
        if(source == ""){
            source = text;
            register_code_blocks();
            break;
        }
        // NB: In order to minimize the reparsing of the local document (source), we don't want to just 
        // replace the whole document (with text), just the bit that has been changed. Therefore we 
        // turn the full change into an incremental one
        // Some of the logic used is based on https://github.com/prabirshrestha/vim-lsp/blob/master/autoload/lsp/utils/diff.vim
        vector<string> old_lines = extract_lines(source);
        vector<string> new_lines = extract_lines(text);

        // The range needed for an incrmental change
        lsp_object first = first_diff(old_lines, new_lines);
        lsp_object end  = last_diff( vector<string>(old_lines.begin() + first.extract_int("line") , old_lines.end() ),
                                     vector<string>(new_lines.begin() + first.extract_int("line") , new_lines.end() ),
                                     first);

        // Updates text to the needed substring for the inc change
        text = extract_text(new_lines, first, end);

        // Adjusts the end position, from negative offset to a regular index (Ex. -1 becomes the index of the last line)
        end.insert("character",  int((end.extract_int("line") == 0) ? 0 : old_lines[ old_lines.size() + end.extract_int("line")].size() + end.extract_int("character") + 1 ));
        end.insert("line", int(end.extract_int("line") + old_lines.size()));
        
        change.insert("range").insert("start", first);
        change.insert("range").insert("end"  , end);
        // NB: We fall through into Incremental
        [[fallthrough]];
    }
    case Incremental:
    {
        vector<size_t> line_starts;
        
        lsp_object range(change.extract_substruct("range"));
        size_t start_ln  = range.extract_substruct("start").extract_int("line");
        size_t start_col = range.extract_substruct("start").extract_int("character");
        size_t end_ln    = range.extract_substruct("end").extract_int("line");
        size_t end_col   = range.extract_substruct("end").extract_int("character");

        // Determine the index at which each line begins, so we can do an inplace updating of the source
        for(size_t idx = 0; source.npos != idx; idx = source.find('\n', idx+1))
            line_starts.push_back(idx == 0 ? 0 : idx + 1); // We want the character to the right of \n (except for the first char)

        int first_untouched = -1;
        // Check for an edit occurring at the very end of the file
        if (start_ln == line_starts.size()){
            source += text;
        }else{
            size_t insert_range_start  = line_starts[start_ln] + start_col;
            size_t insert_range_end    = line_starts[end_ln] + end_col;
            
            // The first line greater than end_ln which starts a new code_block
            // All lines after this can just be shifted down, and don't need reparsing
            // NB: This is the line count BEFORE the insertion
            int delta_line_count = count(text, '\n') - 
                                   count(source.substr(insert_range_start, insert_range_end-insert_range_start), '\n');
            first_untouched = end_ln + delta_line_count + 1;

            // Cleared the range (which can be empty i.e start=end ==> insertion with no replacement) and insert into that gap
            source.erase(insert_range_start, insert_range_end-insert_range_start);
            source.insert(insert_range_start, text);

            // Remove and reregister the remaining code blocks from start_ln to the first unmodified line.
            // These are the ones which may have been changed by an insertion.
            // Anything after the first_untouched will have
            // their source locations shifted (as they were moved by insertion/deletion)
            auto update_remove_or_pass = [start_ln, end_ln, delta_line_count](auto& container, auto& it, symboldecl* sym){
                source_loc new_loc(sym->tok->location);
                size_t ln = new_loc.line;
                if (start_ln <= ln && ln <= end_ln){
                    delete sym;
                    it = container.erase(it);
                }else if(end_ln < ln){
                    // Update the line num
                    new_loc.line += delta_line_count;
                    sym->tok = sym->tok->adjust_location(new_loc);
                    ++it;
                }
                else // ln < start_ln which means that this definition came before the change i.e no update
                    ++it;
            };

            for (auto it = globals.cbegin(); it != globals.cend();)
                update_remove_or_pass(globals, it, *it);
            for (auto it = functions.cbegin(); it != functions.cend();)
                update_remove_or_pass(functions, it, it->second);
        }

        register_code_blocks(start_ln /* Only reregister up to start_ln */, first_untouched /* starting before first_untouched */ , false /* Don't clear the vectors */ );
        break;
    }
    }
}

/* The initialize request is sent from the client to the server.
 * It is sent once as the request after starting up the server.
 * The requests parameter is of type [InitializeParams](#InitializeParams)
 * the response if of type [InitializeResult](#InitializeResult) of a Thenable that\nresolves to such.
 */
class lsp_method_initialize : public lsp_method
{
public:
    lsp_method_initialize(language_server *lang_server) : lsp_method(lang_server) {}
    static constexpr const char* INITIALIZE = "initialize";

    jsonrpc_response *handle(json_object *p)
    {
        lsp_object initialize_params(p);

        lang_server->init_request_received = true;
        lang_server->wspace = new workspace(lang_server->s);

        lsp_object server_capabilities;
        // Documents are synced by sending the full content on open.
        // After that only incremental updates to the document are sent.
        server_capabilities.insert("textDocumentSync", Incremental);

        if (lang_server->is_registered(lsp_method_text_document_completion::TEXT_DOCUMENT_COMPLETION))
        {
            lsp_object completion_options;
            completion_options.insert("resolveProvider", false);
            /* NB: It is worth mentioned that some LS clients will refer to triggerCharacters
             * when determining the position to insert the completion (Ex. vim-lsp).
             * This means that if for example completing @foobar results in unexpected insertions
             * it may be worth considering if '@' should be a trigger char
             */
            completion_options.insert<string>("triggerCharacters", {".", "/", "@", "$", "*"});
            server_capabilities.insert("completionProvider", completion_options);
        }

        lsp_object initialize_result;
        initialize_result.insert("capabilities", server_capabilities);
        initialize_result.insert("serverInfo").insert("name", "systemtap-language-server");
        initialize_result.insert("serverInfo").insert("version", "1.0");
        jsonrpc_response *res = new jsonrpc_response;
        res->set_result(initialize_result.to_json());
        return res;
    }
};

/* The initialized notification is sent from the client to the
 * server after the client is fully initialized and the server
 * is allowed to send requests from the server to the client
 */
class lsp_method_initialized : public lsp_method
{
public:
    lsp_method_initialized(language_server *lang_server) : lsp_method(lang_server) {}
    static constexpr const char* INITIALIZED = "initialized";
    // Pass, no action required
};

/* The document open notification is sent from the client to the server to signal
 * newly opened text documents. The document is now managed by the client
 */
class lsp_method_text_document_did_open : public lsp_method
{
public:
    lsp_method_text_document_did_open(language_server *lang_server) : lsp_method(lang_server) {}
    static constexpr const char* TEXT_DOCUMENT_DID_OPEN = "textDocument/didOpen";

    jsonrpc_response *handle(json_object *p)
    {
        lsp_object did_open_text_document_params(p);
        lang_server->wspace->add_document(did_open_text_document_params.extract_substruct("textDocument"));
        return nullptr;
    }
};

/* The document change notification is sent from the client to the server to signal
 * changes to a text document.
 */
class lsp_method_text_document_did_change : public lsp_method
{
public:
    lsp_method_text_document_did_change(language_server *lang_server) : lsp_method(lang_server) {}
    static constexpr const char* TEXT_DOCUMENT_DID_CHANGE = "textDocument/didChange";

    jsonrpc_response *handle(json_object *p)
    {
        lsp_object did_change_text_document_params(p);
        // Updates document's content.
        lsp_object text_document  (did_change_text_document_params.extract_substruct("textDocument"));
        vector<lsp_object> content_changes(did_change_text_document_params.extract_array<lsp_object>("contentChanges"));

        for(auto change = content_changes.begin(); change != content_changes.end(); ++change){
            // If given a range then we can do an incremental change, otherwise just do a full (replace the entire
            // copy of the doc source with the new given one)
            TextDocumentSyncKind sync_kind = change->contains("range") ? Incremental : Full;
            lang_server->wspace->update_document(
            text_document, *change, sync_kind);
        }
        return nullptr;
    }
};

/* The document close notification is sent from the client to the server when
 * the document got closed in the client.
 */
class lsp_method_text_document_did_close : public lsp_method
{
public:
    lsp_method_text_document_did_close(language_server *lang_server) : lsp_method(lang_server) {}
    static constexpr const char* TEXT_DOCUMENT_DID_CLOSE = "textDocument/didClose";

    jsonrpc_response *handle(json_object *p)
    {
        lsp_object did_close_text_document_params(p);
        lang_server->wspace->remove_document(did_close_text_document_params.extract_substruct("textDocument").extract_string("uri"));
        return nullptr;
    }
};

/* The document save notification is sent from the client to the server when
 * the document got saved in the client
 */
class lsp_method_text_document_did_save : public lsp_method
{
public:
    lsp_method_text_document_did_save(language_server *lang_server) : lsp_method(lang_server) {}
    static constexpr const char* TEXT_DOCUMENT_DID_SAVE = "textDocument/didSave";
    // Pass, no action required
};

/* A shutdown request is sent from the client to the server.
 * It is sent once when the client decides to shutdown the
 * server. The only notification that is sent after a shutdown request
 * is the exit event.
 */
class lsp_method_shutdown : public lsp_method
{
public:
    lsp_method_shutdown(language_server *lang_server) : lsp_method(lang_server) {}
    static constexpr const char* SHUTDOWN = "shutdown";

    jsonrpc_response *handle(json_object *)
    {
        lang_server->shutdown_request_received = true;
        return nullptr;
    }
};

/* The exit event is sent from the client to the server to
 * ask the server to exit its process
 */
class lsp_method_exit : public lsp_method
{
public:
    lsp_method_exit(language_server *lang_server) : lsp_method(lang_server) {}
    static constexpr const char* EXIT = "exit";

    jsonrpc_response *handle(json_object *)
    {
        // Exit should only be called before a server is initialized or after a shutdown
        if (lang_server->init_request_received && !lang_server->shutdown_request_received)
            exit(1);

        delete lang_server->wspace;
        lang_server->running = false;
        return nullptr;
        // The mainloop of the language server, controlled by running, will end and the process
        // will end after cleanup
    }
};

void
language_server::is_method_undefined(jsonrpc_request *req)
{
    if (req->method.empty())
        throw jsonrpc_error(LSPErrCode.MethodNotFound, "Method not specified");
}

void
language_server::is_premature_request(jsonrpc_request *req)
{
    if (!init_request_received && req->method != lsp_method_initialize::INITIALIZE && req->method != lsp_method_exit::EXIT)
        throw jsonrpc_error(LSPErrCode.ServerNotInitialized, "Client sent a request before the server was initialized");
}

void
language_server::is_already_shutdown(jsonrpc_request *req)
{
    if (shutdown_request_received && req->method != lsp_method_exit::EXIT)
        throw jsonrpc_error(LSPErrCode.InvalidRequest, "Client sent a non-exit request/notification after shutdown");
}

void
language_server::is_duplicate_initialization(jsonrpc_request *req)
{
    if (init_request_received && req->method == lsp_method_initialize::INITIALIZE)
        throw jsonrpc_error(LSPErrCode.InvalidRequest, "Client sent duplicate initialization");
}

jsonrpc_response *
language_server::handle_lsp_method(jsonrpc_request *req)
{
    string method = req->method;

    // If any of the following are true, throw an expection (caught in the main loop)
    is_method_undefined(req);
    is_premature_request(req);
    is_duplicate_initialization(req);
    is_already_shutdown(req);

    auto it = registered_lsp_methods.find(method);
    if (it == registered_lsp_methods.end())
        throw jsonrpc_error(LSPErrCode.MethodNotFound, "Unknown method: " + method);
    // Return the response of the handle function for this particular lsp_method_* class instance
    return it->second(this, req->params);
}

language_server::language_server(systemtap_session *sess, unsigned int v) : s{sess}, verbose{v}, c_state{0}
{
    running = init_request_received = shutdown_request_received = false;

    // When registering a method, a map of a method name to handlers is created
    // where the handler<T> will make a new INSTANCE of T, t, and then handle it with the given parameters
    // It is therefore safe to assume that all attributes of t are fresh for each method call

    // Lifecycle messages
    register_lsp_method(lsp_method_initialize::INITIALIZE,   lsp_method::handler<lsp_method_initialize>);
    register_lsp_method(lsp_method_initialized::INITIALIZED, lsp_method::handler<lsp_method_initialized>);
    register_lsp_method(lsp_method_shutdown::SHUTDOWN,       lsp_method::handler<lsp_method_shutdown>);
    register_lsp_method(lsp_method_exit::EXIT,               lsp_method::handler<lsp_method_exit>);

    // Document Synchronization
    register_lsp_method(lsp_method_text_document_did_open::TEXT_DOCUMENT_DID_OPEN,     lsp_method::handler<lsp_method_text_document_did_open>);
    register_lsp_method(lsp_method_text_document_did_change::TEXT_DOCUMENT_DID_CHANGE, lsp_method::handler<lsp_method_text_document_did_change>);
    register_lsp_method(lsp_method_text_document_did_save::TEXT_DOCUMENT_DID_SAVE,     lsp_method::handler<lsp_method_text_document_did_save>);
    register_lsp_method(lsp_method_text_document_did_close::TEXT_DOCUMENT_DID_CLOSE,   lsp_method::handler<lsp_method_text_document_did_close>);

    // Language Features
    register_lsp_method(lsp_method_text_document_completion::TEXT_DOCUMENT_COMPLETION, lsp_method::handler<lsp_method_text_document_completion>);
}

int language_server::run()
{
    // The first time pass_1a is completed, we parse all the library scripts, so do it upon init to remove an initial delay
    string empty = "";
    pass_1(*this->s, empty);
    this->s->register_library_aliases();
    register_standard_tapsets(*this->s);
    transcribe_functions_and_globals(*this->s);

    // TODO: Add TCP option as well, for now just default to stdio
    jsonrpc_connection conn(STDIN_FILENO, STDOUT_FILENO, verbose);

    running = true;
    while (running)
    {
        conn.wait_for_request();
        unique_ptr<jsonrpc_request> request;
        unique_ptr<jsonrpc_response> response;

        try
        {
            request = unique_ptr<jsonrpc_request>(conn.get_request());
        }
        catch (jsonrpc_error &e)
        {
            // If the request parse process fails, e.g.
            // % stap --language-server << /dev/null
            if (verbose)
                cerr << "Error: " << e.what() << endl;
            running = false; // shut things down
            continue;
        }
        
        if (verbose)
          cerr << "---> " << request->method << "(" << (request->is_notification() ? "NOTIF" : "REQ") << ")" << json_object_to_json_string(request->params) << endl;

        try
        {
            response = unique_ptr<jsonrpc_response>(this->handle_lsp_method(request.get()));
        }
        catch (jsonrpc_error &e)
        {
            if (verbose)
                cerr << "Error: " << e.what() << endl;
            response = unique_ptr<jsonrpc_response>(new jsonrpc_response());
            response->set_error(e.get_json());
        }

        if (!request->is_notification() && response && response->result_or_error_set)
        {
            if (verbose)
                cerr << "<--- " << json_object_to_json_string(response->to_json(request.get())) << endl;
            conn.send_response(request.get(), response.get());
        }
    }
    return 0;
}

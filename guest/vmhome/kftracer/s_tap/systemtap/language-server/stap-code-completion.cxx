// Language server code completion
// Copyright (C) 2022 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.
#include "config.h"
#include "staptree.h"
#include "parse.h"
#include "session.h"
#include "elaborate.h"
#include "stap-probe.h"
#include "stap-language-server.h"

#include "jsonrpc.h"
#include <iostream>
#include <signal.h>
#include <sys/select.h>
 #include <sys/stat.h>
#include <dirent.h>
#include <numeric>

/* Utilities */

static string
pc2str(parse_context pc)
{
    switch (pc)
    {
    case con_unknown:  return "con_unknown";
    case con_probe:    return "con_probe";
    case con_function: return "con_function";
    case con_global:   return "con_global";
    case con_embedded: return "con_embedded";
    }
    return "undefined context";
}


void lsp_method_text_document_completion::add_completion_item(string text, string type, string docs, string insert_text)
{
    lsp_object item;
    item.insert("label", text);
    if (!type.empty())
        item.insert("detail", type);
    if (!docs.empty())
        item.insert("documentation", docs);
    if (!insert_text.empty())
    {
        // Use the insertText if provided, this is the text that will be inserted as opposed to label
        item.insert("insertText", insert_text);
        // Using textEdit is preferable to insertText, but is not supported by all clients.
        // insertText is subject to interpretation by the client side
        // setting range's start and end to insert_position and using insert_text means append
        // the NEW part of the completion to the original (Ex. "foob<CURSOR>"
        // would have insert_text "ar" to get "foobar", with insert_position at <CURSOR>)
        lsp_object text_edit;
        text_edit.insert("range").insert("start", insert_position);
        text_edit.insert("range").insert("end", insert_position);
        text_edit.insert("newText", insert_text);
        item.insert("textEdit", text_edit);
    }
    completion_items.push_back(item);
}

void lsp_method_text_document_completion::complete_body()
{
    // Complete the last token that the parser saw
    // TODO: There could be possible improvements here
    // Ex. if we have return f_____ then make sure f_____ is of the specified return type
    string to_complete = "";
    if(lang_server->c_state->tok)
        to_complete =  lang_server->c_state->tok->content;

    if (lang_server->verbose >= 2)
        cerr << "Completing [body]: '" << to_complete << "'" << endl;

    if(startswith(to_complete, "$")){
        for (auto var = target_variables.begin(); var != target_variables.end(); ++var)
        {
            string name = var->first;
            if (startswith(name, to_complete))
            {
                stringstream docs;
                //FIXME: vim-lsp drops * from the docs, so it'll make char** look like char
                docs << name << ":" << var->second; // name:type
                add_completion_item(name, "target var", docs.str(), name.substr(to_complete.size()));
            }
        }
        vector<pair<string, string>> pretty_prints = {
            {"$$vars", "Expands to a character string that is equivalent to sprintf(\"parm1=%x ... parmN=%x var1=%x ... varN=%x\", parm1, ..., parmN, var1, ..., varN) for each variable in scope at the probe point. Some values may be printed as “=?” if their run-time location cannot be found." },
            {"$$locals", "Expands to a subset of $$vars containing only the local variables."},
            {"$$parms", "Expands to a subset of $$vars containing only the function parameters."}
        };
        probe_point* pp = lang_server->c_state->pp;
        if(pp && !pp->components.empty() && pp->components.back()->functor == "return") // $$return should only appear in return probes
            pretty_prints.push_back({"$$return", "Expands to a string that is equivalent to sprintf(\"return=%x\",  $return) if the probed function has a return value, or else an empty string."});
        for (auto pp = pretty_prints.begin(); pp != pretty_prints.end(); ++pp)
        {
            string name = pp->first;
            if (startswith(name, to_complete))
            {
                stringstream docs;
                docs << pp->second;
                add_completion_item(name, "pretty print", docs.str(), name.substr(to_complete.size()));
            }
        }
    }
    else if (!startswith(to_complete, "@"))
    {
        vector<map<string, functiondecl*>> functions = { doc->functions, lang_server->s->functions };
        for (auto f_list = functions.begin(); f_list != functions.end(); ++f_list)
            for (auto func = f_list->begin(); func != f_list->end(); ++func)
            {
                functiondecl *f = func->second;
                if (startswith(f->unmangled_name, to_complete))
                {
                    stringstream docs;
                    f->printsig(docs);
                    add_completion_item(f->unmangled_name.to_string(), "function", docs.str());
                }
            }
        static vector<pair<string, string>> format_print_family = {
            {"print"   , "unknown:(value:any)"},
            {"println" , "unknown:(value:any)"},
            {"printf"  , "unknown:(fmt:string, value:any, ...)"},
            {"printd"  , "unknown:(delimiter:string, value:any, ...)"},
            {"printdln", "unknown:(delimiter:string, value:any, ...)"},
            {"sprint"  , "string:(value:any)"},
            {"sprintf" , "string:(fmt:string, value:any, ...)"}
        };
        for (auto func = format_print_family.begin(); func != format_print_family.end(); ++func)
            if (startswith(func->first, to_complete))
                add_completion_item(func->first, "function", func->first + " " + func->second);

        vector<vector<vardecl*>> globals = { doc->globals, lang_server->s->globals };
        for (auto g_list = globals.begin(); g_list != globals.end(); ++g_list)
            for (auto global = g_list->begin(); global != g_list->end(); ++global)
            {
                vardecl *g = *global;
                if (startswith(g->unmangled_name, to_complete))
                {
                    stringstream docs;
                    g->printsig(docs);
                    add_completion_item(g->unmangled_name.to_string(), "global", docs.str());
                }
            }
        // FIXME: Perhaps I should pull the keywords and atwords from lexer in parse.cxx
        vector<string> keywords = {"probe", "global", "private", "function", "if", "else", "for", "foreach", "in",
                                   "limit", "return", "delete", "while", "break", "continue", "next", "string", "long", "try", "catch"};
        for (auto keyword = keywords.begin(); keyword != keywords.end(); ++keyword)
            if (startswith(*keyword, to_complete))
                add_completion_item(*keyword, "keyword");
    }
    else
    {
        string at_to_complete = to_complete.substr(1); // Remove the @
        for (auto macro = lang_server->s->library_macros.begin(); macro != lang_server->s->library_macros.end(); ++macro)
        {
            macrodecl *m = macro->second;
            string name = m->tok->content;
            if (startswith(name, at_to_complete))
            {
                stringstream docs;
                docs << name << "(" << join_vector(m->formal_args, ", ") << ")";
                // insertText is used here since LS clients have trouble matching @xyz so
                // when matching @x insert only yz
                add_completion_item(name, "macro", docs.str(), name.substr(at_to_complete.size()));
            }
        }
        vector<string> atwords = {"cast", "defined", "probewrite", "entry", "perf", "var", "avg", "count", "sum",
                                  "min", "max", "hist_linear", "hist_log", "const", "variance", "kregister", "uregister", "kderef", "uderef"};
        for (auto atword = atwords.begin(); atword != atwords.end(); ++atword)
            if (startswith(*atword, at_to_complete))
                add_completion_item(*atword, "atword");
    }
}

void lsp_method_text_document_completion::complete_probe_signature()
{
    // Default to an empty string, since the functor may be NULL (Ex. probe process._____)
    string to_complete = "";
    // If we have part of a component then that's what needs completing
    if(lang_server->c_state->comp)
        to_complete = lang_server->c_state->comp->functor.to_string();

    if (lang_server->verbose >= 2)
        cerr << "Completing [probe signature]: '" << to_complete << "'" << endl;

    match_node *node = lang_server->s->pattern_root;
    // Find the current position within the match_node tree (which will be a parent of the result(s))
    // Make sure not to step down prematurely since process could either be complete with 'process.' or incomplete aiming for 'process("'
    const probe_point* pp = lang_server->c_state->pp;
    if(pp && !pp->components.empty() && pp->components.back()->functor != to_complete)
    for (auto pc = lang_server->c_state->pp->components.begin(); pc != lang_server->c_state->pp->components.end(); ++pc)
    {
        string comp = (*pc)->functor.to_string();
        match_key key = match_key(interned_string(comp));
        key.have_parameter =  (*pc)->arg != nullptr;
        if(key.have_parameter)
            key.parameter_type = (*pc)->arg->type;

        for (match_node::sub_map_iterator_t it = node->sub.begin(); it != node->sub.end(); ++it)
            if(key.globmatch(it->first))
            {
                node = it->second;
                break;
            }
    }

    // Find the matching node, which completes the component (to_complete)
    for (match_node::sub_map_iterator_t it = node->sub.begin(); it != node->sub.end(); ++it)
        if (startswith(it->first.str(), to_complete))
        {
            string docs = it->first.str();
            string comp = it->first.name.to_string();

            // LS clients have trouble distinguishing completion items with the same label
            // even if their signatures are different, so having foo, foo(" and foo( serves this purpose
            if (it->first.have_parameter)
                comp = comp + (it->first.parameter_type == pe_string ? "(\"" : "(");

            add_completion_item(comp, "probe component", docs);
        }
}

void lsp_method_text_document_completion::complete_path(string path)
{
    // TODO: PATH can have wildcards which means globbing needs to be supported
    if (path.empty())
        return;

    vector<string> dirs;
    string partial_path = ""; // The path from the start (not including a slash at idx 0) to the last slash (inclusive)
    bool absolute;

    size_t last_slash = path.find_last_of('/');
    string to_complete = path.substr(last_slash + 1);

    if (path[0] == '/')
    {
        absolute = true;
        dirs.push_back("/");

        if (last_slash != string::npos && path.size() >= 1)
            partial_path = path.substr(1, last_slash);
    }
    else
    {
        absolute = false;

        const char *PATH = getenv("PATH");
        if (PATH)
            tokenize(string(PATH), dirs, string(":"));
        dirs.push_back(""); // Look at the current dir as well

        if (last_slash != string::npos)
            partial_path = path.substr(0, last_slash + 1);
    }

    for (auto dir : dirs)
        try
        {
            // If dir is a prefix of partial_path then just look at partial_path
            // otherwise assume that dir is in PATH and partial path is an unexplored tree within dir
            if (!partial_path.empty() && startswith(partial_path, dir.c_str()))
                dir = partial_path;
            else
                dir += (dir.back() != '/' ? "/" : "") + partial_path;
            DIR *dr = opendir(dir.c_str());
            struct dirent *file;
            if (dr)
                while ((file = readdir(dr)) != NULL) {
                    if(0 == strcmp(file->d_name, ".") || 0 == strcmp(file->d_name, "..")) continue;
                    string prefix      = dir + (dir.back() != '/' ? "/" : "") + to_complete;
                    string file_path   = dir + (dir.back() != '/' ? "/" : "") + file->d_name;
                    unsigned char d_type = file->d_type;
                    if (startswith(file_path, prefix) && (d_type == DT_DIR || d_type == DT_REG || d_type == DT_LNK))
                    {
                        struct stat st;
                        if (0 == stat(file_path.c_str(), &st))
                        {
                            if (S_ISDIR(st.st_mode))
                                d_type = DT_DIR;
                            else if(S_ISREG(st.st_mode))
                                d_type = DT_REG;
                            else
                                continue;
                        }
                        else
                            continue;
                        string type = d_type == DT_DIR ? "directory" : "file";
                        if (absolute)
                            add_completion_item(file_path, type, "", string(file_path).substr(prefix.size()));
                        else
                            add_completion_item(file->d_name, type, "", string(file_path).substr(prefix.size()));
                    }
                }
            closedir(dr);
        }
        catch (...)
        {} // If there is an issue just skip
}

void lsp_method_text_document_completion::complete_string()
{
    string to_complete = lang_server->c_state->tok->content.to_string();
    to_complete.erase(0, 1); // Remove the opening quote
    string completion_component = lang_server->c_state->comp->functor.to_string();

    if (lang_server->verbose >= 2)
        cerr << "Completing [string]: '" << to_complete << "'" << endl;

    // TODO: There are still probe components to handle; see man stapprobes
    if (completion_component == "process" || completion_component == "procfs" || completion_component == "library") // The PATHlike components
    {
        return complete_path(to_complete);
    }
    else // Try our luck with wildcard expansion as the param, this logic is the same as that used by stap -L
    {
        try
        {
            // eat the last * token ex. "ma* should complete in the same way "ma would
            // we'll result in ma* completing as ma*n where as ma would complete as main
            bool has_wildcard_tail = to_complete != "" && '*' == to_complete.back();
            if (has_wildcard_tail) to_complete.pop_back();

            // Just 'fix' the probe point by adding a wildcard to the incomplete compnent's arg
            // No need to fully reparse
            probe p;
            p.body = new block();
            probe_point* pp(lang_server->c_state->pp);
            probe_point::component c(completion_component, new literal_string(to_complete + "*"));
            pp->components.push_back(&c);
            p.locations.push_back(pp);

            vector<derived_probe *> dps;
            derive_probes(*lang_server->s, &p, dps, false /* Not optional */, true /* Rethrow semantic errors */);
            for (auto it = dps.begin(); it != dps.end(); ++it)
            {
                derived_probe *dp = *it;
                ostringstream o, docs;
                o << *(*it)->sole_location()->components.back()->arg;

                list<string> args;
                dp->getargs(args);
                if (args.size() > 0)
                {
                    docs << "Target Vars: ";
                    for (auto ia = args.begin(); ia != args.end(); ++ia)
                        docs << *ia << " ";
                }
                // The insertion text is just the tail of the completion
                string insert_text = o.str().substr(to_complete.size());
                if(has_wildcard_tail) insert_text.erase(insert_text.begin()); // We want to remove the first char, since this is the wildcard's spot
                add_completion_item(o.str(), "string", docs.str(), insert_text);
            }
        }
        catch (semantic_error&){}
    }
}

void lsp_method_text_document_completion::complete(string code)
{
    size_t code_start = 0;
    // Skip magic lines [jupyter client specific]
    while (code.size() >= 2 && code[0] == '%' && code[1] == '%')
    {
        code_start = code.find('\n', code_start) + 1;
        code = code.substr(code_start);
    }

    /* Completion relies on parsing the code and seeing where there is a faliure
     * since the faliure point shows what is expected and what is wrong there
     * Pass 1 is called to determine the completion rule using:
     * - The context in which p1 failed
     * - Some information in particular about the failing token
    */ 
    pass_1(*lang_server->s, code);

    if (lang_server->verbose >= 2)
    {
        cerr << "Completion context: " << pc2str(lang_server->c_state->context) << endl;
        cerr << "Code Block:" << code << endl;
    }

    switch (lang_server->c_state->context)
    {
    case con_unknown:
        if (startswith(code, "private "))
            complete(code.substr(strlen("private ")));
        else
            for (string s : {"probe", "global", "private", "function"})
                if (startswith(s, code)) add_completion_item(s, "keyword");
        break;
    case con_probe:
    case con_function:
        if (lang_server->c_state->in_body)
        {
            if(lang_server->c_state->context == con_probe)
            try
            {
                // No need to reparse, just add the probe point to a new body-less probe
                probe p;
                p.locations.push_back(lang_server->c_state->pp);
                p.body = new block();
                vector<derived_probe *> dps;
                derive_probes(*lang_server->s, &p, dps, false /* Not optional */, true /* Rethrow semantic errors */);
                if (0 != dps.size()) // Should only have 1 derived probe
                {
                    list<string> args;
                    dps.front()->getargs(args);
                    for (auto ia = args.begin(); ia != args.end(); ++ia){
                        size_t delim = (*ia).find(':');
                        string name = (*ia).substr(0, delim);
                        string type = (*ia).substr(delim+1);
                        target_variables.push_back({name, type});
                    }
                }
            }
            catch (semantic_error&){}

            /* Completion of a function or probe body, which are the same */
            complete_body();
        }
        else if (lang_server->c_state->context == con_probe && code.size() > strlen("probe"))
        {
            //  code is of the form 'probe...' up to and not incluing a possible body start ({, %{)
            const token *tok = lang_server->c_state->tok;
            if (tok && tok->type == tok_junk && tok->junk_type == tok_junk_unclosed_quote)
                // The special case of string completion
                complete_string();
            else
                complete_probe_signature();
        }
        else
        {
            /* snippets of the form 'function ... {' don't have any sort of completion to do
                likewise neither do snippets of the form 'probe' */
        }
        break;
    case con_global:
    case con_embedded:
    default:
        /* snippets of the form global x = ... don't have any sort of completion to do*/
        /* TODO: For now we do not support C completion */
        break;
    }
    return;
}

jsonrpc_response *
lsp_method_text_document_completion::handle(json_object *p)
{
    lsp_object completion_params(p);

    doc = lang_server->wspace->get_document(completion_params.extract_substruct("textDocument").extract_string("uri"));

    insert_position = completion_params.extract_substruct("position");

    vector<string> lines = doc->get_lines();
    int cursor_line_num = insert_position.extract_int("line");

    // The line has the cursor on it so truncate at cursor pos
    lines.at(cursor_line_num) = lines.at(cursor_line_num).substr(0, insert_position.extract_int("character"));

    string completion_code_block;
    doc->get_last_code_block(lines, completion_code_block, cursor_line_num);

    lsp_object completion_list;
    // Allows the client to be more efficient, as it doesn't need to requery when another char is added, just filter with
    // the same completion list (Ex. completing 'pro' offers 'probe' so don't bother recomputing for 'prob' its the same)
    completion_list.insert("isIncomplete", false);
    complete(completion_code_block);
    completion_list.insert("items", completion_items);

    jsonrpc_response *res = new jsonrpc_response;
    res->set_result(completion_list.to_json());
    return res;
}
// tapset for debuginfod searching
// Copyright (C) 2005-2023 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "session.h"
#include "tapsets.h"
#include "translate.h"
#include "staputil.h"
#include "fnmatch.h"

#if defined(HAVE_LIBDEBUGINFOD) && defined(HAVE_JSON_C) && defined(METADATA_QUERY_ENABLED)

#include <elfutils/debuginfod.h>
#include <json-c/json.h>
#include <unistd.h>

using namespace std;
using namespace __gnu_cxx;

static const string TOK_DEBUGINFOD("debuginfod");
static const string TOK_ARCHIVE("archive");
static const string TOK_PROCESS("process");


struct metadata_result { string archive; string file; };

map<string,metadata_result>
get_buildids(bool has_archive, string archive, string process_path) {
  static unique_ptr <debuginfod_client, void (*)(debuginfod_client*)>
    client (debuginfod_begin(), &debuginfod_end);

  map<string,metadata_result> buildids;
  
  int metadata_fd;
  if((metadata_fd = debuginfod_find_metadata(client.get(), "glob", (char*)process_path.c_str(), NULL)) < 0)
    throw SEMANTIC_ERROR(_("can't retrieve buildids from debuginfod"));

  vector<pid_t> debuginfod_workers;
  json_object *metadata = json_object_from_fd(metadata_fd);
  json_object *metadata_array;
  close(metadata_fd);
  if(!metadata || !json_object_object_get_ex(metadata, "results", &metadata_array))
    throw SEMANTIC_ERROR(_("retrieved invalid buildids from debuginfod"));
  size_t n_bid = json_object_array_length(metadata_array);

  if(0 == n_bid)
    throw SEMANTIC_ERROR(_("can't retrieve buildids from debuginfod"));

  int d_out, d_err;
  json_object *file_metadata, *json_field;
  string buildid;
  for(size_t i = 0; i < n_bid; i++)
  {
    file_metadata = json_object_array_get_idx(metadata_array, i);

    // Reject missing buildid
    if(json_object_object_get_ex(file_metadata, "buildid", &json_field))
      buildid = json_object_get_string(json_field);
    else
      continue;

    // Reject sus buildids
    if(! is_build_id(buildid))
      continue;

    // Reject duplicate buildid - they are equivalent as far as we're concerned
    if (buildids.find(buildid) != buildids.end())
      continue;
    
    // Reject non-executables
    if (! (json_object_object_get_ex(file_metadata, "type", &json_field)
           && 0 == strcmp(json_object_get_string(json_field), "executable")))
      continue;

    // Extract archive name (if any - might be empty).
    string archive_fullname;
    if (json_object_object_get_ex(file_metadata, "archive", &json_field)) {
      archive_fullname = json_object_get_string(json_field);
    }
    string archive_basename = archive_fullname.substr(archive_fullname.find_last_of("/") + 1);

    // Reject mismatching archive, if user specified
    if (has_archive) {
      // documented as basename comparison, so no FNM_PATHNAME needed        
      if(0 != fnmatch(archive.c_str(), archive_basename.c_str(), 0))
        continue;
    }

    // Extract file name (mandatory)
    string filename;
    if (json_object_object_get_ex(file_metadata, "file", &json_field))
      filename = json_object_get_string(json_field);
    else
      continue;
    
    metadata_result af; af.archive = archive_basename; af.file = filename;
    buildids.insert(make_pair(buildid, af));

    debuginfod_workers.push_back(stap_spawn_piped(0, {"debuginfod-find", "executable", buildid}, NULL, &d_out, &d_err));
    // NB: we don't really have to preload the debuginfo - a probe point may not call for dwarf
    // NB: but if it does, we'd suffer latency by downloading one at a time, sequentially
    debuginfod_workers.push_back(stap_spawn_piped(0, {"debuginfod-find", "debuginfo" , buildid}, NULL, &d_out, &d_err));
  }
  json_object_put(metadata);

  // Make sure all the executables/debuginfo are found before continuing
  for(auto worker_pid = debuginfod_workers.begin(); worker_pid != debuginfod_workers.end(); ++worker_pid)
    if(*worker_pid > 0) stap_waitpid(0, *worker_pid);

  return buildids;
}


// ------------------------------------------------------------------------
// debuginfod_builder derived probes
// ------------------------------------------------------------------------
struct debuginfod_builder: public derived_probe_builder
{
public:
  void build(systemtap_session & sess, probe * base,
    probe_point * location,
    literal_map_t const & parameters,
    vector<derived_probe *> & finished_results);

  void build_with_suffix(systemtap_session & sess, probe * base,
    probe_point * location,
    literal_map_t const & parameters,
    std::vector<derived_probe *> & finished_results,
    std::vector<probe_point::component *> const & suffix);

  virtual string name() { return "debuginfod builder"; }
};


void
debuginfod_builder::build(systemtap_session & sess, probe * base,
  probe_point * location,
  literal_map_t const & parameters,
  vector<derived_probe *> & finished_results)
{
  interned_string archive;
  interned_string process_path;
  bool has_debuginfod = has_null_param (parameters, TOK_DEBUGINFOD);
  bool has_process    = get_param (parameters, TOK_PROCESS, process_path);
  bool has_archive    = get_param (parameters, TOK_ARCHIVE, archive);
  
  if(!has_debuginfod || !has_process)
    throw SEMANTIC_ERROR(_("the probe must be of the form debuginfod.[.archive(\"foobar\")]process(\"foo/bar\").**{...}"));

  // The matching buildids from the archives/debuginfod
  map<string,metadata_result> buildids = get_buildids(has_archive, archive, process_path);

  for(auto it = buildids.begin(); it != buildids.end(); ++it) {
    interned_string buildid = it->first;
    interned_string m_archive = it->second.archive;
    interned_string m_file = it->second.file;

    probe *subbase = base;
    probe_point *subbase_pp = location;

    if (sess.verbose > 3) {
      clog << "from: ";
      subbase->printsig(clog);
    }
    
    // If the probe archive/process names were globs, interject an intermediate
    // probe into the chain that resolves the globs.
    if (process_path.find_first_of("*?[{") != string::npos ||
        (has_archive && archive.find_first_of("*?[{") != string::npos)) // was some globbing performed?
      {
        subbase_pp = new probe_point(*location);
        if (has_archive) {
          assert (subbase_pp->components[1]->functor == TOK_ARCHIVE);
          subbase_pp->components[1] = new probe_point::component(TOK_ARCHIVE,
                                                                 new literal_string(m_archive),
                                                                 false);
          assert (subbase_pp->components[2]->functor == TOK_PROCESS);
          subbase_pp->components[2] = new probe_point::component(TOK_PROCESS,
                                                                 new literal_string(m_file),
                                                                 false);
        } else {
          assert (subbase_pp->components[1]->functor == TOK_PROCESS);
          subbase_pp->components[1] = new probe_point::component(TOK_PROCESS,
                                                                 new literal_string(m_file),
                                                                 false);          
        }
        // subbase_pp->well_formed = true; // XXX: true, unless the FUNCTION etc. stuff has *
        subbase = new probe(base, subbase_pp);

        if (sess.verbose > 3) {
          clog << " through: ";
          subbase->printsig(clog);
        }
      }

    // OK time for the new process("buildid").foo probe
    probe_point *derived_pp = new probe_point(*subbase_pp);
    derived_pp->components.erase(derived_pp->components.begin()); // Remove the 'debuginfod'
    if (has_archive)
      derived_pp->components.erase(derived_pp->components.begin()); // Remove the 'archive' too
    assert (derived_pp->components[0]->functor == TOK_PROCESS);
    // NB: since probe_points are shallow-copied, we must not modify a
    // preexisting component, but create new
    derived_pp->components[0] = new probe_point::component(TOK_PROCESS,
                                                           new literal_string(buildid),
                                                           false);
    derived_pp->optional = true; // even if not from a glob, to accept wrong-arch buildids
    // derived_pp->well_formed = true; // XXX: true, unless the FUNCTION etc. stuff has *
    probe *derived_p = new probe(subbase, derived_pp);
    
    if (sess.verbose > 3) {
      clog << " to: ";
      derived_p->printsig(clog);
      clog << endl;
    }

    if (sess.verbose > 2)
      clog << _F("resolved debuginfod archive %s file %s -> buildid %s archive %s file %s",
                 has_archive ? archive.to_string().c_str() : "?",
                 process_path.to_string().c_str(),
                 buildid.to_string().c_str(),
                 m_archive.to_string().c_str(),
                 m_file.to_string().c_str())
           << endl;
    
    vector<derived_probe *> results;
    derive_probes(sess, derived_p, results, false, true);
    finished_results.insert(finished_results.end(), results.begin(), results.end());
  }
}

void
debuginfod_builder::build_with_suffix(systemtap_session & sess, probe * base,
    probe_point * location,
    literal_map_t const &,
    std::vector<derived_probe *> & finished_results,
    std::vector<probe_point::component *> const & suffix){

    /* Extract the parameters for the head of the probe
     * The probes are of the form debuginfod[.archive(**)].process("foo/bar").**. 
     * So the length should always be TWO (2) or THREE (3), 
     * when a archive parameter is provided.
     */
    unsigned num_param = location->components.size() - suffix.size();
    assert(num_param == 2 || num_param == 3);

    literal_map_t param_map;
    for (unsigned i=0; i<num_param; i++)
      param_map[location->components[i]->functor] = location->components[i]->arg;

    build(sess, base, location, param_map, finished_results);
    return;
}


void
register_tapset_debuginfod(systemtap_session& s)
{
    match_node* root = s.pattern_root;
    derived_probe_builder *builder = new debuginfod_builder();

    // All suffixes will get caught and processed by build_with_suffix
    
    //debuginfod.process()
    root->bind(TOK_DEBUGINFOD)->bind_str(TOK_PROCESS)->bind(builder);

    //debuginfod.archive().process()
    root->bind(TOK_DEBUGINFOD)->bind_str(TOK_ARCHIVE)->bind_str(TOK_PROCESS)->bind(builder);

}

#else /* no DEBUGINFOD and/or no JSON */

void
register_tapset_debuginfod(systemtap_session& s)
{
  (void) s;
}

#endif

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */

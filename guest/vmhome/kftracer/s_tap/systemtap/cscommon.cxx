/*
 Compile-server and client common functions
 Copyright (C) 2011 Red Hat Inc.

 This file is part of systemtap, and is free software.  You can
 redistribute it and/or modify it under the terms of the GNU General
 Public License (GPL); either version 2, or (at your option) any
 later version.
*/
#include "config.h"

// Disable the code in this file if NSS is not available
#if HAVE_NSS
#include "staputil.h"
#include "cscommon.h"
#include "nsscommon.h"

#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <iomanip>

extern "C"
{
#include <ssl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
}

using namespace std;

cs_protocol_version::~cs_protocol_version ()
{
  assert (this->v);
  free ((void*)this->v);
}

const cs_protocol_version &
cs_protocol_version::operator= (const char *v)
{
  if (this->v)
    free ((void *)this->v);
  this->v = strdup (v);
  return *this;
}

bool
cs_protocol_version::operator< (const cs_protocol_version &that) const
{
  // Compare the levels of each version in turn.
  vector<string> these_tokens;
  tokenize (this->v, these_tokens, ".");
  vector<string> those_tokens;
  tokenize (that.v, those_tokens, ".");

  unsigned this_limit = these_tokens.size ();
  unsigned that_limit = those_tokens.size ();
  unsigned i;
  for (i = 0; i < this_limit && i < that_limit; ++i)
    {
      char *e;
      unsigned long this_level = strtoul (these_tokens[i].c_str (), & e, 0);
      assert (! *e);
      unsigned long that_level = strtoul (those_tokens[i].c_str (), & e, 0);
      assert (! *e);
      if (this_level > that_level)
	return false;
      if (this_level < that_level)
	return true;
    }

  // If the other version has more components, then this one is less than that one.
  if (i < that_limit)
    {
      assert (i == this_limit);
      return true;
    }
  // This version is greater than or equal to that one.
  return false;
}

int
read_from_file (const string &fname, cs_protocol_version &data)
{
  // C++ streams may not set errno in the even of a failure. However if we
  // set it to 0 before each operation and it gets set during the operation,
  // then we can use its value in order to determine what happened.
  string dataStr;
  errno = 0;
  ifstream f (fname.c_str ());
  if (! f.good ())
    {
      clog << _F("Unable to open file '%s' for reading: ", fname.c_str());
      goto error;
    }

  // Read the data;
  errno = 0;
  f >> dataStr;
  if (f.fail ())
    {
      clog << _F("Unable to read from file '%s': ", fname.c_str());
      goto error;
    }

  data = dataStr.c_str ();

  // NB: not necessary to f.close ();
  return 0; // Success

 error:
  if (errno)
    clog << strerror (errno) << endl;
  else
    clog << _("unknown error") << endl;
  return 1; // Failure
}

string get_cert_serial_number (const CERTCertificate *cert)
{
  ostringstream serialNumber;
  serialNumber << hex << setfill('0') << right;
  for (unsigned i = 0; i < cert->serialNumber.len; ++i)
    {
      if (i > 0)
	serialNumber << ':';
      serialNumber << setw(2) << (unsigned)cert->serialNumber.data[i];
    }
  return serialNumber.str ();
}

int
mok_sign_file (const std::string &mok_fingerprint,
	       const std::string &mok_path,
	       const std::string &kernel_build_tree,
	       const std::string &name)
{
  string mok_directory = mok_path + "/" + mok_fingerprint;

  vector<string> cmd
    {
      kernel_build_tree + "/scripts/sign-file",
      "sha512",
      mok_directory + MOK_PRIVATE_CERT_FILE,
      mok_directory + MOK_PUBLIC_CERT_FILE,
      name
    };

  return stap_system (0, cmd);
}


void
generate_mok(string &mok_fingerprint, void report_error (const string& msg, int logit))
{
  string mok_path = server_cert_db_path() + "/moks";
  vector<string> cmd;
  int rc;
  char tmpdir[PATH_MAX] = { '\0' };
  string public_cert_path, private_cert_path, destdir;
  string msg;
  mode_t old_umask;
  int retlen;
// The default MOK config text used when creating new MOKs. This text is 
// saved to the MOK config file and can be modified by the administrator.
  const char mok_config_text[] =
  "[ req ]\n"						
  "default_bits = 4096\n"				
  "distinguished_name = req_distinguished_name\n"	
  "prompt = no\n"					
  "x509_extensions = myexts\n"				
  "\n"							
  "[ req_distinguished_name ]\n"			
  "O = Systemtap\n"					
  "CN = Systemtap module signing key\n"			
  "\n"							
  "[ myexts ]\n"					
  "basicConstraints=critical,CA:FALSE\n"		
  "keyUsage=digitalSignature\n"				
  "subjectKeyIdentifier=hash\n"				
  "authorityKeyIdentifier=keyid\n";

  mok_fingerprint.clear ();

  // Set umask so that everything is private.
  old_umask = umask(077);

  DIR *dirp = opendir (mok_path.c_str());
  if (dirp == NULL)
    {
      if (create_dir (mok_path.c_str (), 0755) != 0)
	report_error (_F("Unable to find or create the MOK directory %s: %s",
			 mok_path.c_str (), strerror (errno)), false);
    }
  else
    closedir (dirp);

  // Make sure the config file exists. If not, create it with default
  // contents.
  string config_path = mok_path + MOK_CONFIG_FILE;
  if (! file_exists (config_path))
    {
      ofstream config_stream;
      config_stream.open (config_path.c_str ());
      if (! config_stream.good ())
        {
	  msg = _F("Could not open MOK config file %s: %s",
		   config_path.c_str (), strerror (errno));
	  report_error (msg, true);
	  goto cleanup;
	}
      config_stream << mok_config_text;
      config_stream.close ();
    }

  // Make a temporary directory to store results in.
  retlen = snprintf (tmpdir, PATH_MAX, "%s/stap-server.XXXXXX", mok_path.c_str ());
  if (retlen < 0 || retlen >= PATH_MAX)
    {
      msg = _F("Could not create %s name", "temporary directory");
      report_error (msg, true);
      tmpdir[0] = '\0';
      goto cleanup;
    }

  if (mkdtemp (tmpdir) == NULL)
    {
      msg = _F("Could not create temporary directory %s: %s", tmpdir, 
		       strerror (errno));
      report_error (msg, true);
      tmpdir[0] = '\0';
      goto cleanup;
    }

  // Actually generate key using openssl.
  public_cert_path = tmpdir + string (MOK_PUBLIC_CERT_FILE);
  private_cert_path = tmpdir + string (MOK_PRIVATE_CERT_FILE);

  cmd =
    {
      "openssl", "req", "-new", "-nodes", "-utf8",
      "-sha256", "-days", "36500", "-batch", "-x509",
      "-config", config_path,
      "-outform", "DER",
      "-out", public_cert_path,
      "-keyout", private_cert_path
    };
  rc = stap_system (0, cmd);
  if (rc != 0) 
    {
      msg = _F("Generating MOK failed, rc = %d", rc);
      report_error (msg, true);
      goto cleanup;
    }

  // Grab the fingerprint from the cert.
  if (read_cert_info_from_file (public_cert_path, mok_fingerprint)
      != SECSuccess)
    goto cleanup;

  // Once we know the fingerprint, rename the temporary directory.
  destdir = mok_path + "/" + mok_fingerprint;
  if (rename (tmpdir, destdir.c_str ()) < 0)
    {
      msg = _F("Could not rename temporary directory %s to %s: %s",
	       tmpdir, destdir.c_str (), strerror (errno));
      report_error (msg, true);
      goto cleanup;
    }

  // Restore the old umask.
  umask(old_umask);

  return;

cleanup:
  // Remove the temporary directory.
  cmd = { "rm", "-rf", tmpdir };
  rc = stap_system (0, cmd);
  if (rc != 0)
    {
      msg = _("Error in tmpdir cleanup");
      report_error (msg, true);
    }
  mok_fingerprint.clear ();

  // Restore the old umask.
  umask(old_umask);
  return;
}


static void
client_error (const string &msg, int logit __attribute__ ((unused)) = 0)
{
  cerr << _(msg.c_str()) << endl;
}

int
sign_module(const string &tmpdir, const string &module_filename,
	    std::vector<std::string> mok_fingerprints,
	    const string &mok_root,
	    const string &kernel_build_tree)
{
  string module_src_path = tmpdir + "/" + module_filename;

  PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);
  /* Set the cert database password callback. */
  PK11_SetPasswordFunc (nssPasswordCallback);

  struct stat file_info;
  if(stat(server_cert_db_path().c_str(), &file_info) != 0)
    {
      vector<string> cmd;
      int rc;
      cmd = { "mkdir", "-p", server_cert_db_path().c_str() };
      rc = stap_system (0, cmd);
      if (rc != 0)
          cerr << (_F("Error creating server cert db path \"%s\"", //
                   server_cert_db_path().c_str())) << endl;
    }

  NSSInitContext *context = nssInitContext (server_cert_db_path().c_str());
  if (!context)
    cerr << "nssInitContext failed for " << server_cert_db_path();

  bool module_signed = false;
  int rc;
  string mok_fingerprint;
  string mok_path;
      
  if (mok_root.empty())
    // --sign-module
    mok_path = server_cert_db_path() + "/moks";
  else
    // --sign-module=PATH
    mok_path = mok_root;

  for (auto it = mok_fingerprints.cbegin(); it != mok_fingerprints.cend(); it++)
    {
      mok_fingerprint = *it;
      if (! mok_dir_valid_p (*it, mok_path, false, client_error))
	continue;
      
      if ((rc = mok_sign_file (mok_fingerprint, mok_path, kernel_build_tree, module_src_path)) == 0)
	{
	  cerr << (_F("Module signed with MOK, fingerprint \"%s\"", //
		      mok_fingerprint.c_str())) << endl;
	  module_signed = true;
	  break;
	}
    }

  if (! module_signed)
    {
      generate_mok (mok_fingerprint, client_error);
      cerr << (_("Running sign-file failed\n"))
	   << (_F("There is no machine owner key (MOK) in common with this system.\nUse the following command to import a MOK into this system, then reboot:\n\n\t# sudo mokutil --import %s/moks/%s/signing_key.x509",
		  server_cert_db_path().c_str(), mok_fingerprint.c_str())) << endl;
    }
  
  PR_Cleanup ();
  return !module_signed;
}


bool
mok_dir_valid_p (const string &mok_fingerprint, const string &mok_path, bool verbose,
		 void report_error (const string &msg, int logit))
{
  string mok_dir = mok_path + "/" + mok_fingerprint;
  DIR *dirp = opendir (mok_dir.c_str());

  if (dirp == NULL)
    {
      // We can't open the directory. Just quit.
      if (verbose)
	report_error (_F("Could not open server MOK fingerprint directory %s: %s",
			 mok_dir.c_str(), strerror(errno)), true);
      return false;
    }

  // Find both the x509 certificate and private key files.
  string mok_private_cert_path = mok_dir + MOK_PRIVATE_CERT_FILE;
  string mok_public_cert_path = mok_dir + MOK_PUBLIC_CERT_FILE;
  
  if (access(mok_private_cert_path.c_str(), R_OK ) != 0 
      || access(mok_public_cert_path.c_str(), R_OK ) != 0)
    {
      if (verbose)
	report_error (_F("Could not find server MOK files in directory %s",
			 mok_dir.c_str ()), true);
      return false;
    }

  // Grab info from the cert.
  string fingerprint;
  if (read_cert_info_from_file (mok_dir + MOK_PUBLIC_CERT_FILE, fingerprint)
      == SECSuccess)
    {
      // Make sure the fingerprint from the certificate matches the
      // directory name.
      if (fingerprint != mok_fingerprint)
        {
	  if (verbose)
	      report_error (_F("Server MOK directory name '%s' doesn't match fingerprint from certificate %s",
			       mok_dir.c_str(), fingerprint.c_str()), true);
	  return false;
	}
    }
  return true;
}


#endif /* HAVE_NSS */

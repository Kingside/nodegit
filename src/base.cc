/*
Copyright (c) 2011, Tim Branyen @tbranyen <tim@tabdeveloper.com>
*/

#include <v8.h>
#include <node.h>
#include <node_events.h>

#include "../vendor/libgit2/include/git2.h"

#include "reference.h"
#include "sig.h"
#include "../include/error.h"
#include "../include/blob.h"
#include "repo.h"
#include "oid.h"
#include "object.h"
#include "commit.h"
#include "revwalk.h"
#include "tree.h"
#include "tree_entry.h"

namespace {

extern "C" void init(Handle<v8::Object> target) {
  HandleScope scope;

  Reference::Initialize(target);
  Sig::Initialize(target);
  GitError::Initialize(target);
  GitBlob::Initialize(target);
  Oid::Initialize(target);
  GitObject::Initialize(target);
  Repo::Initialize(target);
  GitCommit::Initialize(target);
  RevWalk::Initialize(target);
  GitTree::Initialize(target);
  GitTreeEntry::Initialize(target);
}

}

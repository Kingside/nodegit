/*
 * Copyright 2011, Tim Branyen @tbranyen <tim@tabdeveloper.com>
 * @author Michael Robinson @codeofinterest <mike@pagesofinterest.net>
 *
 * Dual licensed under the MIT and GPL licenses.
 */

#include <string.h>
#include <v8.h>
#include <node.h>

#include "../vendor/libgit2/include/git2.h"

#include "../include/diff_list.h"
#include "../include/error.h"

#include "../include/functions/string.h"

using namespace v8;
using namespace node;

void GitDiffList::Initialize(Handle<Object> target) {
  HandleScope scope;

  Local<FunctionTemplate> tpl = FunctionTemplate::New(New);

  tpl->InstanceTemplate()->SetInternalFieldCount(1);
  tpl->SetClassName(String::NewSymbol("DiffList"));

  NODE_SET_PROTOTYPE_METHOD(tpl, "treeToTree", TreeToTree);
  NODE_SET_PROTOTYPE_METHOD(tpl, "close", Close);

  constructor_template = Persistent<Function>::New(tpl->GetFunction());
  target->Set(String::NewSymbol("DiffList"), constructor_template);
}

git_diff_list* GitDiffList::GetValue() {
  return this->diffList;
}

void GitDiffList::SetValue(git_diff_list* diffList) {
  this->diffList = diffList;
}

Handle<Value> GitDiffList::New(const Arguments& args) {
  HandleScope scope;

  GitDiffList *diffList = new GitDiffList();
  diffList->Wrap(args.This());

  return scope.Close(args.This());
}

void GitDiffList::Close() {
  git_diff_list_free(this->diffList);
  this->diffList = NULL;
}

Handle<Value> GitDiffList::Close(const Arguments& args) {
  HandleScope scope;

  GitDiffList *diffList = ObjectWrap::Unwrap<GitDiffList>(args.This());
  diffList->Close();

  return scope.Close(Undefined());
}

Handle<Value> GitDiffList::TreeToTree(const Arguments& args) {
  HandleScope scope;

  if(args.Length() == 0 || !(args[0]->IsObject() || args[0]->IsString())) {
    return ThrowException(Exception::Error(String::New("Repo is required and must be an Object or String")));
  }

  if(args.Length() == 1 || !(args[1]->IsObject() || args[1]->IsString())) {
    return ThrowException(Exception::Error(String::New("Old Oid/SHA is required and must be an Object or String")));
  }

  if(args.Length() == 2 || !(args[2]->IsObject() || args[2]->IsString())) {
    return ThrowException(Exception::Error(String::New("New Oid/SHA is required and must be an Object or String")));
  }

  if(args.Length() == 3 || !args[3]->IsFunction()) {
    return ThrowException(Exception::Error(String::New("Callback is required and must be a Function.")));
  }

  TreeToTreeBaton *baton = new TreeToTreeBaton();
  baton->request.data = baton;
  baton->error = NULL;
  baton->diffList = ObjectWrap::Unwrap<GitDiffList>(args.This());
  baton->repo = ObjectWrap::Unwrap<GitRepo>(args[0]->ToObject())->GetValue();

  if (args[1]->IsObject()) {
    baton->oldOid = ObjectWrap::Unwrap<GitOid>(args[1]->ToObject())->GetValue();
  } else {
    baton->oldSha = stringArgToString(args[1]->ToString());
  }

  if (args[2]->IsObject()) {
    baton->newOid = ObjectWrap::Unwrap<GitOid>(args[2]->ToObject())->GetValue();
  } else {
    baton->newSha = stringArgToString(args[2]->ToString());
  }

  baton->callback = Persistent<Function>::New(Local<Function>::Cast(args[3]));

  uv_queue_work(uv_default_loop(), &baton->request, TreeToTreeWork, TreeToTreeAfterWork);

  return Undefined();
}

void GitDiffList::TreeToTreeWork(uv_work_t *req) {
  TreeToTreeBaton *baton = static_cast<TreeToTreeBaton *>(req->data);


  // Prepare git_oid's
  git_oid *oldOid = &baton->oldOid;
  if (!baton->oldSha.empty()) {
    int returnCode = git_oid_fromstr(oldOid, baton->oldSha.c_str());
    if (returnCode != GIT_OK) {
      printf("oldOid\n");
      baton->error = giterr_last();
      return;
    }
  }
  git_oid *newOid = &baton->newOid;
  if (!baton->newSha.empty()) {
    int returnCode = git_oid_fromstr(newOid, baton->newSha.c_str());
    if (returnCode != GIT_OK) {
      baton->error = giterr_last();
      return;
    }
  }

  // Get commits
  git_commit* oldCommit = NULL;
  int returnCode = git_commit_lookup(&oldCommit, baton->repo, oldOid);
  if (returnCode != GIT_OK) {
    baton->error = giterr_last();
    return;
  }

  git_commit* newCommit = NULL;
  returnCode = git_commit_lookup(&newCommit, baton->repo, oldOid);
  if (returnCode != GIT_OK) {
    baton->error = giterr_last();
    return;
  }

  // Prepare trees
  git_tree* oldTree = NULL;
  returnCode = git_commit_tree(&oldTree, oldCommit);
  if (returnCode != GIT_OK) {
    printf("oldTree\n");
    baton->error = giterr_last();
    return;
  }
  git_tree* newTree = NULL;
  returnCode = git_commit_tree(&newTree, newCommit);
  if (returnCode != GIT_OK) {
    printf("newTree\n");
    baton->error = giterr_last();
    return;
  }

  baton->rawDiffList = NULL;
  returnCode = git_diff_tree_to_tree(&baton->rawDiffList, baton->repo, oldTree, newTree, NULL);
  if (returnCode != GIT_OK) {
    printf("t2t\n");
    baton->error = giterr_last();
  }
}

void GitDiffList::TreeToTreeAfterWork(uv_work_t *req) {
  HandleScope scope;

  TreeToTreeBaton *baton = static_cast<TreeToTreeBaton *>(req->data);
  delete req;

  if (baton->error) {
    Local<Value> argv[1] = {
      GitError::WrapError(baton->error)
    };

    TryCatch try_catch;
    baton->callback->Call(Context::GetCurrent()->Global(), 1, argv);
    if (try_catch.HasCaught()) {
      node::FatalException(try_catch);
    }
  } else {

    baton->diffList->SetValue(baton->rawDiffList);

    Handle<Value> argv[2] = {
      Local<Value>::New(Null()),
      baton->diffList->handle_
    };

    TryCatch try_catch;
    baton->callback->Call(Context::GetCurrent()->Global(), 2, argv);
    if (try_catch.HasCaught()) {
      node::FatalException(try_catch);
    }
  }
}

Handle<Value> GitDiffList::Iterate(const Arguments& args) {
  HandleScope scope;

  GitDiffList*  diffList = ObjectWrap::Unwrap<GitDiffList>(args.This());

  if (diffList->GetValue() == NULL) {
    return ThrowException(Exception::Error(String::New("Diff list has not been initialized.")));
  }

  if(args.Length() == 0 || !args[0]->IsFunction()) {
    return ThrowException(Exception::Error(String::New("Diff callback is required and must be a Function.")));
  }

  if(args.Length() == 1 || !args[1]->IsFunction()) {
    return ThrowException(Exception::Error(String::New("Hunk callback is required and must be a Function.")));
  }

  if(args.Length() == 2 || !args[2]->IsFunction()) {
    return ThrowException(Exception::Error(String::New("Line callback is required and must be a Function.")));
  }

  if(args.Length() == 3 || !args[3]->IsFunction()) {
    return ThrowException(Exception::Error(String::New("End callback is required and must be a Function.")));
  }

  IterateBaton* baton = new IterateBaton();
  baton->rawDiffList = diffList->GetValue();

  baton->fileCallback = Persistent<Function>::New(Local<Function>::Cast(args[0]));
  baton->hunkCallback = Persistent<Function>::New(Local<Function>::Cast(args[1]));
  baton->lineCallback = Persistent<Function>::New(Local<Function>::Cast(args[2]));
  baton->endCallback = Persistent<Function>::New(Local<Function>::Cast(args[3]));

  uv_thread_create(&baton->threadId, IterateWork, &baton);

  return Undefined();
}

void GitDiffList::IterateWork(void *arg) {
  printf("here\n");
}


Persistent<Function> GitDiffList::constructor_template;
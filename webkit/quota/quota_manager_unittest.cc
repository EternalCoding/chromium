// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "testing/gtest/include/gtest/gtest.h"

#include <vector>

#include "base/file_util.h"
#include "base/memory/scoped_callback_factory.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_temp_dir.h"
#include "base/message_loop.h"
#include "base/message_loop_proxy.h"
#include "base/stl_util-inl.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebStorageQuotaError.h"
#include "third_party/WebKit/Source/WebKit/chromium/public/WebStorageQuotaType.h"
#include "webkit/quota/mock_storage_client.h"
#include "webkit/quota/quota_database.h"
#include "webkit/quota/quota_manager.h"

using base::MessageLoopProxy;
using WebKit::WebStorageQuotaError;
using WebKit::WebStorageQuotaType;

namespace quota {

namespace {
struct MockOriginData {
  const char* origin;
  StorageType type;
  int64 usage;
};
}  // anonymous namespace

class QuotaManagerTest : public testing::Test {
 public:
  QuotaManagerTest()
      : callback_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)) {
  }

  void SetUp() {
    ASSERT_TRUE(data_dir_.CreateUniqueTempDir());
    quota_manager_ = new QuotaManager(
        false /* is_incognito */,
        data_dir_.path(),
        MessageLoopProxy::CreateForCurrentThread(),
        MessageLoopProxy::CreateForCurrentThread());
    additional_callback_count_ = 0;
  }

  void TearDown() {
    // Make sure the quota manager cleans up correctly.
    quota_manager_ = NULL;
    MessageLoop::current()->RunAllPending();
  }

 protected:
  MockStorageClient* CreateClient(
      const MockOriginData* mock_data, size_t mock_data_size) {
    MockStorageClient* client = new MockStorageClient(quota_manager_->proxy());
    for (size_t i = 0; i < mock_data_size; ++i) {
      client->AddMockOriginData(GURL(mock_data[i].origin),
                                mock_data[i].type,
                                mock_data[i].usage);
    }
    return client;
  }

  void RegisterClient(MockStorageClient* client) {
    quota_manager_->proxy()->RegisterClient(client);
  }

  void GetUsageAndQuota(const GURL& origin, StorageType type) {
    quota_status_ = kQuotaStatusUnknown;
    usage_ = -1;
    quota_ = -1;
    quota_manager_->GetUsageAndQuota(origin, type,
        callback_factory_.NewCallback(
            &QuotaManagerTest::DidGetUsageAndQuota));
  }

  void GetTemporaryGlobalQuota() {
    quota_status_ = kQuotaStatusUnknown;
    quota_ = -1;
    quota_manager_->GetTemporaryGlobalQuota(
        callback_factory_.NewCallback(
            &QuotaManagerTest::DidGetQuota));
  }

  void SetTemporaryGlobalQuota(int64 new_quota) {
    quota_status_ = kQuotaStatusUnknown;
    quota_ = -1;
    quota_manager_->SetTemporaryGlobalQuota(new_quota,
        callback_factory_.NewCallback(
            &QuotaManagerTest::DidGetQuota));
  }

  void GetPersistentHostQuota(const std::string& host) {
    quota_status_ = kQuotaStatusUnknown;
    quota_ = -1;
    quota_manager_->GetPersistentHostQuota(host,
        callback_factory_.NewCallback(
            &QuotaManagerTest::DidGetHostQuota));
  }

  void SetPersistentHostQuota(const std::string& host, int64 new_quota) {
    quota_status_ = kQuotaStatusUnknown;
    quota_ = -1;
    quota_manager_->SetPersistentHostQuota(host, new_quota,
        callback_factory_.NewCallback(
            &QuotaManagerTest::DidGetHostQuota));
  }

  void GetGlobalUsage(StorageType type) {
    usage_ = -1;
    quota_manager_->GetGlobalUsage(type,
        callback_factory_.NewCallback(
            &QuotaManagerTest::DidGetGlobalUsage));
  }

  void GetHostUsage(const std::string& host, StorageType type) {
    usage_ = -1;
    quota_manager_->GetHostUsage(host, type,
        callback_factory_.NewCallback(
            &QuotaManagerTest::DidGetHostUsage));
  }

  void RunAdditionalUsageAndQuotaTask(const GURL& origin, StorageType type) {
    quota_manager_->GetUsageAndQuota(origin, type,
        callback_factory_.NewCallback(
            &QuotaManagerTest::DidGetUsageAndQuotaAdditional));
  }

  void DidGetUsageAndQuota(QuotaStatusCode status, int64 usage, int64 quota) {
    quota_status_ = status;
    usage_ = usage;
    quota_ = quota;
  }

  void DidGetQuota(QuotaStatusCode status, int64 quota) {
    quota_status_ = status;
    quota_ = quota;
  }

  void DidGetHostQuota(QuotaStatusCode status,
                       const std::string& host, int64 quota) {
    quota_status_ = status;
    host_ = host;
    quota_ = quota;
  }

  void DidGetGlobalUsage(int64 usage) {
    usage_ = usage;
  }

  void DidGetHostUsage(const std::string&, int64 usage) {
    usage_ = usage;
  }

  void set_additional_callback_count(int c) { additional_callback_count_ = c; }
  int additional_callback_count() const { return additional_callback_count_; }
  void DidGetUsageAndQuotaAdditional(
      QuotaStatusCode status, int64 usage, int64 quota) {
    ++additional_callback_count_;
  }

  QuotaManager* quota_manager() const { return quota_manager_.get(); }
  void set_quota_manager(QuotaManager* quota_manager) {
    quota_manager_ = quota_manager;
  }

  QuotaStatusCode status() const { return quota_status_; }
  int64 usage() const { return usage_; }
  int64 quota() const { return quota_; }

 private:
  ScopedTempDir data_dir_;
  base::ScopedCallbackFactory<QuotaManagerTest> callback_factory_;

  scoped_refptr<QuotaManager> quota_manager_;

  QuotaStatusCode quota_status_;
  std::string host_;
  int64 usage_;
  int64 quota_;

  int additional_callback_count_;

  DISALLOW_COPY_AND_ASSIGN(QuotaManagerTest);
};

TEST_F(QuotaManagerTest, GetUsageAndQuota_Simple) {
  const static MockOriginData kData[] = {
    { "http://foo.com/",     kStorageTypeTemporary,  10 },
    { "http://foo.com/",     kStorageTypePersistent, 80 },
  };
  RegisterClient(CreateClient(kData, ARRAYSIZE_UNSAFE(kData)));

  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(80, usage());
  EXPECT_EQ(0, quota());

  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(10, usage());
  EXPECT_LE(0, quota());
  EXPECT_GE(QuotaManager::kTemporaryStorageQuotaMaxSize, quota());
  int64 quota_returned_for_foo = quota();

  GetUsageAndQuota(GURL("http://bar.com/"), kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(0, usage());
  EXPECT_EQ(quota_returned_for_foo - 10, quota());
}

TEST_F(QuotaManagerTest, GetUsage_NoClient) {
  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(0, usage());

  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(0, usage());

  GetHostUsage("foo.com", kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(0, usage());

  GetHostUsage("foo.com", kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(0, usage());

  GetGlobalUsage(kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(0, usage());

  GetGlobalUsage(kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(0, usage());
}

TEST_F(QuotaManagerTest, GetUsage_EmptyClient) {
  RegisterClient(CreateClient(NULL, 0));
  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(0, usage());

  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(0, usage());

  GetHostUsage("foo.com", kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(0, usage());

  GetHostUsage("foo.com", kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(0, usage());

  GetGlobalUsage(kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(0, usage());

  GetGlobalUsage(kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(0, usage());
}

TEST_F(QuotaManagerTest, GetTemporaryUsageAndQuota_MultiOrigins) {
  const static MockOriginData kData[] = {
    { "http://foo.com/",        kStorageTypeTemporary,  10 },
    { "http://foo.com:8080/",   kStorageTypeTemporary,  20 },
    { "http://bar.com/",        kStorageTypeTemporary,   5 },
    { "https://bar.com/",       kStorageTypeTemporary,   7 },
    { "http://baz.com/",        kStorageTypeTemporary,  30 },
    { "http://foo.com/",        kStorageTypePersistent, 40 },
  };
  RegisterClient(CreateClient(kData, ARRAYSIZE_UNSAFE(kData)));

  // This time explicitly sets a temporary global quota.
  SetTemporaryGlobalQuota(100);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(100, quota());

  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(10 + 20, usage());

  // The returned quota must be equal to (global_quota - other_origins_usage).
  EXPECT_EQ(100 - (5 + 7 + 30), quota());

  GetUsageAndQuota(GURL("http://bar.com/"), kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(5 + 7, usage());
  EXPECT_EQ(100 - (10 + 20 + 30), quota());
}

TEST_F(QuotaManagerTest, GetUsage_MultipleClients) {
  const static MockOriginData kData1[] = {
    { "http://foo.com/",     kStorageTypeTemporary,  10 },
    { "http://bar.com/",     kStorageTypeTemporary,  20 },
    { "http://bar.com/",     kStorageTypePersistent, 50 },
  };
  const static MockOriginData kData2[] = {
    { "https://foo.com/",    kStorageTypeTemporary,  30 },
    { "http://example.com/", kStorageTypePersistent, 40 },
  };
  RegisterClient(CreateClient(kData1, ARRAYSIZE_UNSAFE(kData1)));
  RegisterClient(CreateClient(kData2, ARRAYSIZE_UNSAFE(kData2)));

  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(10 + 30, usage());

  GetUsageAndQuota(GURL("http://bar.com/"), kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(50, usage());

  GetGlobalUsage(kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(10 + 20 + 30, usage());

  GetGlobalUsage(kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(40 + 50, usage());
}

TEST_F(QuotaManagerTest, GetTemporaryUsage_WithModify) {
  const static MockOriginData kData[] = {
    { "http://foo.com/",     kStorageTypeTemporary,  10 },
    { "http://foo.com:1/",   kStorageTypeTemporary,  20 },
  };
  MockStorageClient* client = CreateClient(kData, ARRAYSIZE_UNSAFE(kData));
  RegisterClient(client);

  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(10 + 20, usage());

  client->ModifyMockOriginDataSize(
      GURL("http://foo.com/"), kStorageTypeTemporary, 20);
  client->ModifyMockOriginDataSize(
      GURL("http://foo.com:1/"), kStorageTypeTemporary, -5);
  client->ModifyMockOriginDataSize(
      GURL("http://bar.com/"), kStorageTypeTemporary, 33);

  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(10 + 20 + 20 - 5, usage());

  GetUsageAndQuota(GURL("http://bar.com/"), kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(33, usage());
}

TEST_F(QuotaManagerTest, GetTemporaryUsageAndQuota_WithAdditionalTasks) {
  const static MockOriginData kData[] = {
    { "http://foo.com/",        kStorageTypeTemporary,  10 },
    { "http://foo.com:8080/",   kStorageTypeTemporary,  20 },
    { "http://bar.com/",        kStorageTypeTemporary,  13 },
    { "http://foo.com/",        kStorageTypePersistent, 40 },
  };
  RegisterClient(CreateClient(kData, ARRAYSIZE_UNSAFE(kData)));
  SetTemporaryGlobalQuota(100);

  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypeTemporary);
  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypeTemporary);
  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(10 + 20, usage());
  EXPECT_EQ(100 - 13, quota());

  set_additional_callback_count(0);
  RunAdditionalUsageAndQuotaTask(GURL("http://foo.com/"),
                                 kStorageTypeTemporary);
  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypeTemporary);
  RunAdditionalUsageAndQuotaTask(GURL("http://bar.com/"),
                                 kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(10 + 20, usage());
  EXPECT_EQ(100 - 13, quota());
  EXPECT_EQ(2, additional_callback_count());
}

TEST_F(QuotaManagerTest, GetTemporaryUsageAndQuota_NukeManager) {
  const static MockOriginData kData[] = {
    { "http://foo.com/",        kStorageTypeTemporary,  10 },
    { "http://foo.com:8080/",   kStorageTypeTemporary,  20 },
    { "http://bar.com/",        kStorageTypeTemporary,  13 },
    { "http://foo.com/",        kStorageTypePersistent, 40 },
  };
  RegisterClient(CreateClient(kData, ARRAYSIZE_UNSAFE(kData)));
  SetTemporaryGlobalQuota(100);

  set_additional_callback_count(0);
  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypeTemporary);
  RunAdditionalUsageAndQuotaTask(GURL("http://foo.com/"),
                                 kStorageTypeTemporary);
  RunAdditionalUsageAndQuotaTask(GURL("http://bar.com/"),
                                 kStorageTypeTemporary);

  // Nuke before waiting for callbacks.
  set_quota_manager(NULL);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaErrorAbort, status());
}

TEST_F(QuotaManagerTest, GetAndSetPerststentHostQuota) {
  RegisterClient(CreateClient(NULL, 0));

  GetPersistentHostQuota("foo.com");
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(0, quota());

  SetPersistentHostQuota("foo.com", 100);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(100, quota());

  GetPersistentHostQuota("foo.com");
  SetPersistentHostQuota("foo.com", 200);
  GetPersistentHostQuota("foo.com");
  SetPersistentHostQuota("foo.com", 300);
  GetPersistentHostQuota("foo.com");
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(300, quota());
}

TEST_F(QuotaManagerTest, GetAndSetPersistentUsageAndQuota) {
  RegisterClient(CreateClient(NULL, 0));

  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(0, usage());
  EXPECT_EQ(0, quota());

  SetPersistentHostQuota("foo.com",100);
  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(0, usage());
  EXPECT_EQ(100, quota());
}

TEST_F(QuotaManagerTest, GetPersistentUsageAndQuota_MultiOrigins) {
  const static MockOriginData kData[] = {
    { "http://foo.com/",        kStorageTypePersistent, 10 },
    { "http://foo.com:8080/",   kStorageTypePersistent, 20 },
    { "https://foo.com/",       kStorageTypePersistent, 13 },
    { "https://foo.com:8081/",  kStorageTypePersistent, 19 },
    { "http://bar.com/",        kStorageTypePersistent,  5 },
    { "https://bar.com/",       kStorageTypePersistent,  7 },
    { "http://baz.com/",        kStorageTypePersistent, 30 },
    { "http://foo.com/",        kStorageTypeTemporary,  40 },
  };
  RegisterClient(CreateClient(kData, ARRAYSIZE_UNSAFE(kData)));

  SetPersistentHostQuota("foo.com", 100);
  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(10 + 20 + 13 + 19, usage());
  EXPECT_EQ(100, quota());
}

TEST_F(QuotaManagerTest, GetPersistentUsage_WithModify) {
  const static MockOriginData kData[] = {
    { "http://foo.com/",     kStorageTypePersistent,  10 },
    { "http://foo.com:1/",   kStorageTypePersistent,  20 },
  };
  MockStorageClient* client = CreateClient(kData, ARRAYSIZE_UNSAFE(kData));
  RegisterClient(client);

  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(10 + 20, usage());

  client->ModifyMockOriginDataSize(
      GURL("http://foo.com/"), kStorageTypePersistent, 20);
  client->ModifyMockOriginDataSize(
      GURL("http://foo.com:1/"), kStorageTypePersistent, -5);
  client->ModifyMockOriginDataSize(
      GURL("http://bar.com/"), kStorageTypePersistent, 33);

  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(10 + 20 + 20 - 5, usage());

  GetUsageAndQuota(GURL("http://bar.com/"), kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(33, usage());
}

TEST_F(QuotaManagerTest, GetPersistentUsageAndQuota_WithAdditionalTasks) {
  const static MockOriginData kData[] = {
    { "http://foo.com/",        kStorageTypePersistent,  10 },
    { "http://foo.com:8080/",   kStorageTypePersistent,  20 },
    { "http://bar.com/",        kStorageTypePersistent,  13 },
    { "http://foo.com/",        kStorageTypeTemporary,   40 },
  };
  RegisterClient(CreateClient(kData, ARRAYSIZE_UNSAFE(kData)));
  SetPersistentHostQuota("foo.com",100);

  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypePersistent);
  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypePersistent);
  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(10 + 20, usage());
  EXPECT_EQ(100, quota());

  set_additional_callback_count(0);
  RunAdditionalUsageAndQuotaTask(GURL("http://foo.com/"),
                                 kStorageTypePersistent);
  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypePersistent);
  RunAdditionalUsageAndQuotaTask(GURL("http://bar.com/"),
                                 kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaStatusOk, status());
  EXPECT_EQ(10 + 20, usage());
  EXPECT_EQ(2, additional_callback_count());
}

TEST_F(QuotaManagerTest, GetPersistentUsageAndQuota_NukeManager) {
  const static MockOriginData kData[] = {
    { "http://foo.com/",        kStorageTypePersistent,  10 },
    { "http://foo.com:8080/",   kStorageTypePersistent,  20 },
    { "http://bar.com/",        kStorageTypePersistent,  13 },
    { "http://foo.com/",        kStorageTypeTemporary,   40 },
  };
  RegisterClient(CreateClient(kData, ARRAYSIZE_UNSAFE(kData)));
  SetPersistentHostQuota("foo.com", 100);

  set_additional_callback_count(0);
  GetUsageAndQuota(GURL("http://foo.com/"), kStorageTypePersistent);
  RunAdditionalUsageAndQuotaTask(GURL("http://foo.com/"),
                                 kStorageTypePersistent);
  RunAdditionalUsageAndQuotaTask(GURL("http://bar.com/"),
                                 kStorageTypePersistent);

  // Nuke before waiting for callbacks.
  set_quota_manager(NULL);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(kQuotaErrorAbort, status());
}

TEST_F(QuotaManagerTest, GetUsage_Simple) {
  const static MockOriginData kData[] = {
    { "http://foo.com/",   kStorageTypePersistent,       1 },
    { "http://foo.com:1/", kStorageTypePersistent,      20 },
    { "http://bar.com/",   kStorageTypeTemporary,      300 },
    { "https://buz.com/",  kStorageTypeTemporary,     4000 },
    { "http://buz.com/",   kStorageTypeTemporary,    50000 },
    { "http://bar.com:1/", kStorageTypePersistent,  600000 },
    { "http://foo.com/",   kStorageTypeTemporary,  7000000 },
  };
  RegisterClient(CreateClient(kData, ARRAYSIZE_UNSAFE(kData)));

  GetGlobalUsage(kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(usage(), 1 + 20 + 600000);

  GetGlobalUsage(kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(usage(), 300 + 4000 + 50000 + 7000000);

  GetHostUsage("foo.com", kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(usage(), 1 + 20);

  GetHostUsage("buz.com", kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(usage(), 4000 + 50000);
}

TEST_F(QuotaManagerTest, GetUsage_WithModification) {
  const static MockOriginData kData[] = {
    { "http://foo.com/",   kStorageTypePersistent,       1 },
    { "http://foo.com:1/", kStorageTypePersistent,      20 },
    { "http://bar.com/",   kStorageTypeTemporary,      300 },
    { "https://buz.com/",  kStorageTypeTemporary,     4000 },
    { "http://buz.com/",   kStorageTypeTemporary,    50000 },
    { "http://bar.com:1/", kStorageTypePersistent,  600000 },
    { "http://foo.com/",   kStorageTypeTemporary,  7000000 },
  };

  MockStorageClient* client = CreateClient(kData, ARRAYSIZE_UNSAFE(kData));
  RegisterClient(client);

  GetGlobalUsage(kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(usage(), 1 + 20 + 600000);

  client->ModifyMockOriginDataSize(
      GURL("http://foo.com/"), kStorageTypePersistent, 80000000);

  GetGlobalUsage(kStorageTypePersistent);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(usage(), 1 + 20 + 600000 + 80000000);

  GetGlobalUsage(kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(usage(), 300 + 4000 + 50000 + 7000000);

  client->ModifyMockOriginDataSize(
      GURL("http://foo.com/"), kStorageTypeTemporary, 1);

  GetGlobalUsage(kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(usage(), 300 + 4000 + 50000 + 7000000 + 1);

  GetHostUsage("buz.com", kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(usage(), 4000 + 50000);

  client->ModifyMockOriginDataSize(
      GURL("http://buz.com/"), kStorageTypeTemporary, 900000000);

  GetHostUsage("buz.com", kStorageTypeTemporary);
  MessageLoop::current()->RunAllPending();
  EXPECT_EQ(usage(), 4000 + 50000 + 900000000);
}

}  // namespace quota

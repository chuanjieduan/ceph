#ifndef CEPH_RGW_SERVICES_RADOS_H
#define CEPH_RGW_SERVICES_RADOS_H


#include "rgw/rgw_service.h"

#include "include/rados/librados.hpp"

class RGWAccessListFilter {
public:
  virtual ~RGWAccessListFilter() {}
  virtual bool filter(string& name, string& key) = 0;
};

struct RGWAccessListFilterPrefix : public RGWAccessListFilter {
  string prefix;

  explicit RGWAccessListFilterPrefix(const string& _prefix) : prefix(_prefix) {}
  bool filter(string& name, string& key) override {
    return (prefix.compare(key.substr(0, prefix.size())) == 0);
  }
};

class RGWS_RADOS : public RGWService
{
  std::vector<std::string> get_deps();
public:
  RGWS_RADOS(CephContext *cct) : RGWService(cct, "rados") {}

  int create_instance(const string& conf, RGWServiceInstanceRef *instance) override;
};

struct rgw_rados_ref {
  rgw_pool pool;
  string oid;
  string key;
  librados::IoCtx ioctx;
};

class RGWSI_RADOS : public RGWServiceInstance
{
  std::vector<librados::Rados> rados;
  uint32_t next_rados_handle{0};
  RWLock handle_lock;
  std::map<pthread_t, int> rados_map;

  int load(const string& conf, std::map<std::string, RGWServiceInstanceRef>& deps) override;

  librados::Rados* get_rados_handle();
  int open_pool_ctx(const rgw_pool& pool, librados::IoCtx& io_ctx);
  int pool_iterate(librados::IoCtx& ioctx,
                   librados::NObjectIterator& iter,
                   uint32_t num, vector<rgw_bucket_dir_entry>& objs,
                   RGWAccessListFilter *filter,
                   bool *is_truncated);

public:
  RGWSI_RADOS(RGWService *svc, CephContext *cct): RGWServiceInstance(svc, cct),
                                                  handle_lock("rados_handle_lock") {}

  uint64_t instance_id();

  class Obj {
    friend class RGWSI_RADOS;

    RGWSI_RADOS *rados_svc{nullptr};
    rgw_rados_ref ref;

    void init(const rgw_raw_obj& obj);

    Obj(RGWSI_RADOS *_rados_svc, const rgw_raw_obj& _obj) : rados_svc(_rados_svc) {
      init(_obj);
    }

  public:
    Obj() {}
    Obj(const Obj& o) : rados_svc(o.rados_svc),
                        ref(o.ref) {}

    Obj(Obj&& o) : rados_svc(o.rados_svc),
                   ref(std::move(o.ref)) {}

    Obj& operator=(Obj&& o) {
      rados_svc = o.rados_svc;
      ref = std::move(o.ref);
      return *this;
    }

    int open();

    int operate(librados::ObjectWriteOperation *op);
    int operate(librados::ObjectReadOperation *op, bufferlist *pbl);
    int aio_operate(librados::AioCompletion *c, librados::ObjectWriteOperation *op);

    uint64_t get_last_version();
  };

  class Pool {
    friend class RGWSI_RADOS;

    RGWSI_RADOS *rados_svc{nullptr};
    rgw_pool pool;

    Pool(RGWSI_RADOS *_rados_svc, const rgw_pool& _pool) : rados_svc(_rados_svc),
                                                           pool(_pool) {}

    Pool(RGWSI_RADOS *_rados_svc) : rados_svc(_rados_svc) {}
  public:
    Pool() {}
    Pool(const Pool& p) : rados_svc(p.rados_svc),
                          pool(p.pool) {}

    int create(const std::vector<rgw_pool>& pools, std::vector<int> *retcodes);
    int lookup(const rgw_pool& pool);

    struct List {
      Pool& pool;

      struct Ctx {
        bool initialized{false};
        librados::IoCtx ioctx;
        librados::NObjectIterator iter;
        RGWAccessListFilter *filter{nullptr};
      } ctx;

      List(Pool& _pool) : pool(_pool) {}

      int init(const string& marker, RGWAccessListFilter *filter = nullptr);
      int get_next(int max,
                   std::list<string> *oids,
                   bool *is_truncated);
    };

    List op() {
      return List(*this);
    }

    friend class List;
  };

  Obj obj(const rgw_raw_obj& o) {
    return Obj(this, o);
  }

  Pool pool() {
    return Pool(this);
  }

  Pool pool(const rgw_pool& p) {
    return Pool(this, p);
  }

  friend class Obj;
  friend class Pool;
  friend class Pool::List;
};

#endif

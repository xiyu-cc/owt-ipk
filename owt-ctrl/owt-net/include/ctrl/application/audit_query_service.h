#pragma once

#include "ctrl/ports/interfaces.h"

namespace ctrl::application {

class AuditQueryService {
public:
  explicit AuditQueryService(ports::IAuditRepository& audits);

  domain::ListPage<domain::AuditEntry, domain::AuditListCursor> list(
      const domain::AuditListFilter& filter) const;

private:
  ports::IAuditRepository& audits_;
};

} // namespace ctrl::application

#include "ctrl/application/audit_query_service.h"

#include <stdexcept>
#include <string>

namespace ctrl::application {

AuditQueryService::AuditQueryService(ports::IAuditRepository& audits) : audits_(audits) {}

domain::ListPage<domain::AuditEntry, domain::AuditListCursor> AuditQueryService::list(
    const domain::AuditListFilter& filter) const {
  domain::ListPage<domain::AuditEntry, domain::AuditListCursor> page;
  std::string error;
  if (!audits_.list(filter, page, error)) {
    throw std::runtime_error("list audits failed: " + error);
  }
  return page;
}

} // namespace ctrl::application

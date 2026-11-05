#pragma once
#include "compilers.h"
#include "io/io.h"
namespace io
{
    MLW_FORCE_INLINE IoError invalidInput() { return {IoError::Kind::InvalidInput, 22}; } // EINVAL

    MLW_FORCE_INLINE bool validateOpen(const OpenOptions &o){        // ---- shared validation ----
        if (!o.read_ && !o.write_ && !o.append_)
            return false; // no access mode requested at all
        if (!o.write_ && !o.append_ && (o.truncate_ || o.create_ || o.createNew_))
            return false; // can't create/truncate a read-only open
        if (o.append_ && o.truncate_ && !o.createNew_)
            return false; // truncate is meaningless in append mode
        return true;
    }
} // namespace fs

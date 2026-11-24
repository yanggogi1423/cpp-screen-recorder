#pragma once
#include "RecordingFiles.h"
namespace luma {
bool markRecoveryFolder(const QString &folder);
FileSaveResult saveRecordingAndCleanRecovery(const QString &source, const QString &target,
                                             bool overwrite, const QString &root,
                                             const QString &folder);
} // namespace luma

#pragma once

#include <filesystem>

class QCommandLineParser;

namespace KailleraExport
{

bool IsReplayExportRequested(const QCommandLineParser& parser);
int RunReplayExportFromCommandLine(const QCommandLineParser& parser);

} // namespace KailleraExport

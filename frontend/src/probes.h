// The command-line probes (--romm-probe, --network, --qr, --storage and the
// rest): each reports and exits without a window. Moved out of main.cpp in
// the restructure (#398).
//
// earlyProbe and storageProbe run at the two points of start-up where main()
// calls them, which is what makes each safe on a console somebody is using:
// earlyProbe before the drives and the storage tree, storageProbe once the
// tree and the core directory are settled and before SDL. Each returns the
// exit code when a probe ran, and nothing when none was asked for. The
// options are main()'s parsed flags, named as main() names them.

#pragma once

#include <optional>

struct EarlyProbeOptions {
    bool sameNamesMode = false;
    const char* coreDir = nullptr;
    const char* rommAddress = nullptr;
    bool accountsTestMode = false;
    bool playersTestMode = false;
    bool batteryTestMode = false;
    bool volumeTestMode = false;
    bool playtimeTestMode = false;
    bool discTestMode = false;
    bool filesTestMode = false;
    bool vectrexSheetsTestMode = false;
    bool gpuChoiceTestMode = false;
    int padsSeconds = 0;
    bool firstRunRulesMode = false;
    bool firstRunWriteMode = false;
    const char* qrText = nullptr;
    const char* qrPbm = nullptr;
    bool networkProbeMode = false;
    bool networkScan = false;
    bool gpuProbeMode = false;
};

std::optional<int> earlyProbe(const EarlyProbeOptions& o);

struct StorageProbeOptions {
    bool firstRunProbeMode = false;
    const char* firstRunStep = nullptr;
    bool firstRunServerCheck = false;
    const char* rommAddress = nullptr;
    int romProbeId = 0;
    const char* romProbeExts = nullptr;
    bool rommProbeMode = false;
    bool rommPair = false;
    bool coreOptionsAudit = false;
    bool coreOptionsDetail = false;
    const char* corePath = nullptr;
    const char* romPath = nullptr;
    int autoLaunchId = 0;
    const char* coreDir = nullptr;
    bool accountsProbeMode = false;
    const char* serverCheckAddress = nullptr;
    int keepersRomId = 0;
    bool storageReport = false;
    int removeAccountId = 0;
    bool removeAnyway = false;
};

std::optional<int> storageProbe(const StorageProbeOptions& o);

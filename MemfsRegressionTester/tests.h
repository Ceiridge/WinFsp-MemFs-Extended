#pragma once

#include <string>

struct TestOptions {
	std::wstring WorkDir;      // <target>\memfs-regtest
	int MassFiles = 20000;     // T5
	int MassDirs = 50;         // T5
	int StressSeconds = 60;    // T7
	int SdScanLimit = 400;     // T2: max files per full-directory SD scan
	int ProvokeRounds = 200;   // T8
};

void T1_RenameOverOpenFile(const TestOptions& opt);
void T2_SecurityDescriptorIntegrity(const TestOptions& opt);
void T3_NamedStreamOutlivesMainFile(const TestOptions& opt);
void T4_ParallelCreateCollision(const TestOptions& opt);
void T5_MassFilesAndCapacity(const TestOptions& opt);
void T6_ListingAfterRenameChains(const TestOptions& opt);
void T7_Concurrency(const TestOptions& opt);
void T8_UnlinkAndRecreateProvocation(const TestOptions& opt);

// Runs the T2 sweep over every file below opt.WorkDir; used after each other test.
void T2_SweepAfter(const TestOptions& opt, const wchar_t* afterTest);

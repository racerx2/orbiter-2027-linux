// not upstream (head-track): NaturalPoint's firmware found in the user's own TrackIR software by its signature, ported from linuxtrack's hashing (MIT, LICENSE-linuxtrack)

#ifndef __HTFIRMWARE_H
#define __HTFIRMWARE_H

#include <cstdint>
#include <string>
#include <vector>

namespace ht {

struct FwSignature {
	const char *name;    // "tir5.fw"
	int64_t size;
	uint16_t fast;       // FastHash of its first 32 bytes
	const char *md5, *sha1;
};
const std::vector<FwSignature> &FirmwareSignatures (); // tir4.fw, tir5.fw, tir5v2.fw (linuxtrack's spec.txt)

class FastHash { // a rolling hash of the last 32 bytes
public:
	static const int length = 32;
	uint16_t Hash (uint8_t val);
private:
	uint8_t buffer[length] = {};
	int index = 0;
};

struct FwFound { std::string name, from; }; // "tir5.fw", the file it was in
// every signed firmware in the folder's files (*.dll, *.exe, *.dat; *.fw.gz as they are), written to destDir as <name>.gz
std::vector<FwFound> FindFirmware (const std::string &folder, const std::string &destDir, std::string &err);
std::vector<FwFound> FindFirmware (const std::string &folder, const std::string &destDir, std::string &err, const std::vector<FwSignature> &sigs);

} // namespace ht

#endif // !__HTFIRMWARE_H

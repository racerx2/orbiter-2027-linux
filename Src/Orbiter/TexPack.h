// not upstream: planet texture packs (Config/TexturePacks.cfg), the missing check and the installer

#ifndef __TEXPACK_H
#define __TEXPACK_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct TexPart {
	std::string name, md5;
	uint64_t size = 0;
};

// a zip of Textures/<Body>/<Layer>/, or with parts an archive <Body>/Archive/<Layer>.tree
struct TexPack {
	std::string zip, body, layer, md5;
	uint64_t zipsize = 0, rawsize = 0, files = 0; // archive: download size, file size, part count
	std::vector<TexPart> parts;
};

struct TexPackList {
	std::string url; // release folder the zips are downloaded from
	std::vector<TexPack> pack;
};

// progress: bytes done of the current step; returning false cancels
typedef std::function<bool(uint64_t done)> TexPackProgress;

// texdir is PlanetTexDir as Orbiter spells it (".\\Textures\\"); paths resolve like the client's
bool TexPackRead (const std::string &cfgfile, TexPackList &list, std::string &err);
bool TexPackPresent (const std::string &texdir, const std::string &body, const std::string &layer);
bool TexPackPresent (const std::string &texdir, const TexPack &p);
std::vector<TexPack> TexPackMissing (const TexPackList &list, const std::string &texdir);
uint64_t TexPackResume (const std::string &texdir, const TexPack &p); // archive bytes kept from an earlier try
uint64_t TexPackSpare (const TexPack &p); // temporary disk space while installing
std::string TexPackWorkDir (const std::string &texdir);
// partial archives of packs no longer listed, and what an earlier install left in the work folder; returns what went
std::vector<std::string> TexPackClean (const TexPackList &list, const std::string &texdir);
bool TexPackMd5 (const std::string &file, std::string &md5);
bool TexPackUnzip (const std::string &zipfile, const TexPack &p, const std::string &texdir, const TexPackProgress &progress, std::string &err);
bool TexPackDownload (const std::string &url, const std::string &file, const TexPackProgress &progress, std::string &err);
bool TexPackInstall (const TexPackList &list, const TexPack &p, const std::string &texdir,
	const TexPackProgress &download, const TexPackProgress &unpack, std::string &err);

#endif // !__TEXPACK_H

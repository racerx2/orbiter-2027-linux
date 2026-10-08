// not upstream: planet texture packs (Config/TexturePacks.cfg), the missing check and the installer

#ifndef __TEXPACK_H
#define __TEXPACK_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct TexPack {
	std::string zip, body, layer, md5;
	uint64_t zipsize = 0, rawsize = 0, files = 0;
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
std::vector<TexPack> TexPackMissing (const TexPackList &list, const std::string &texdir);
std::string TexPackWorkDir (const std::string &texdir);
bool TexPackMd5 (const std::string &file, std::string &md5);
bool TexPackUnzip (const std::string &zipfile, const TexPack &p, const std::string &texdir, const TexPackProgress &progress, std::string &err);
bool TexPackDownload (const std::string &url, const std::string &file, const TexPackProgress &progress, std::string &err);
bool TexPackInstall (const TexPackList &list, const TexPack &p, const std::string &texdir,
	const TexPackProgress &download, const TexPackProgress &unpack, std::string &err);

#endif // !__TEXPACK_H

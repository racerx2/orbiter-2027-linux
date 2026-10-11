// not upstream: "@gltf:<off>:<len>:<b64>:<sliceoff>:<slicelen>:<file>" image refs of glTF meshes (Src/Orbiter/GltfLoader.h)
#pragma once
#include <QByteArray>
#include <cstdio>
#include <cstring>

struct VkGltfRef { unsigned long long off, len, sliceOff, sliceLen; int b64; const char *file; };

inline bool VkParseGltfRef (const char *name, VkGltfRef &r)
{
	int used = 0;
	r.b64 = 0;
	if (!name || strncmp (name, "@gltf:", 6) || sscanf (name + 6, "%llu:%llu:%d:%llu:%llu:%n", &r.off, &r.len, &r.b64, &r.sliceOff, &r.sliceLen, &used) != 5 || !used || !name[6 + used]) return false;
	r.file = name + 6 + used;
	return true;
}

// bytes read from the range to image bytes: base64 when flagged (JSON "\/" and the url-safe alphabet, as GltfBase64), then the slice
inline bool VkGltfImageBytes (const VkGltfRef &r, QByteArray &b)
{
	if (r.b64) b = QByteArray::fromBase64 (b.replace ('\\', QByteArray ()).replace ('-', '+').replace ('_', '/'), QByteArray::Base64Encoding);
	if (r.sliceOff > (unsigned long long)b.size() || (r.sliceLen && r.sliceOff + r.sliceLen > (unsigned long long)b.size())) return false;
	b = b.mid ((qsizetype)r.sliceOff, r.sliceLen ? (qsizetype)r.sliceLen : -1);
	return true;
}

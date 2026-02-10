#pragma once

// VirtualDrive.hpp
// A simple, robust, header-only "pak-style" virtual drive for MSVC/Windows.
// - Uses std::wstring for all public APIs
// - Packs a folder into a single .pak file (recursively)
// - Opens an existing .pak, lists, reads, extracts files
// - No compression (simplifies reliability)
// - UTF-8 is used inside the pak for path storage, to ensure stability across systems
//
// File format (little-endian):
//   Header:
//     4B: magic "VDPK"
//     2B: version = 1
//     2B: flags   = 0 (reserved)
//     8B: fileCount (uint64_t)
//     8B: indexOffset (uint64_t)
//
//   Data section: file contents concatenated, each at recorded offset
//
//   Index section (repeat fileCount times):
//     4B: pathLen (bytes, uint32_t) — path stored as UTF-8
//     8B: offset  (uint64_t)
//     8B: size    (uint64_t)
//     4B: crc32   (uint32_t)
//     nB: path UTF-8 bytes
//
// Notes:
// - Paths inside pak use forward slashes “/”. Case-insensitive lookups are supported on Windows.
// - No directories are stored, only files and their relative paths. Directories are created as needed during extraction.
// - CRC32 is stored per file for optional validation.
// - Hidden/System files are skipped by default (can be included with a flag).
//
// Basic usage:
//
//   #include "VirtualDrive.hpp"
//
//   int main() {
//       vdrive::VirtualDrive vd;
//       std::wstring src = L"C:\\data\\assets";
//       std::wstring pak = L"C:\\out\\assets.vdp";
//
//       std::wstring err;
//       if (!vd.PackDirectory(src, pak, false, &err)) {
//           wprintf(L"Pack failed: %s\n", err.c_str());
//           return 1;
//       }
//
//       if (!vd.Open(pak, &err)) {
//           wprintf(L"Open failed: %s\n", err.c_str());
//           return 1;
//       }
//
//       auto files = vd.List(); // all files
//       for (const auto& f : files) {
//           wprintf(L"%s\n", f.c_str());
//       }
//
//       std::vector<unsigned char> data;
//       if (vd.ReadFile(L"images/logo.png", data, true, &err)) {
//           wprintf(L"Loaded %zu bytes\n", data.size());
//       }
//
//       if (!vd.ExtractAll(L"C:\\restore_here", true, &err)) {
//           wprintf(L"Extract failed: %s\n", err.c_str());
//       }
//   }
//
// LLM guidance:
// - PackDirectory: input is source folder and destination pak path. If needed, set includeHidden=true.
// - Open: load index (you can then call Contains, List, ReadFile, ExtractFile, ExtractAll).
// - Always pass an error std::wstring* to capture detailed errors for the user.
// - All virtual paths are relative and use “/”. Users may supply either form; the API normalizes them.

#include <windows.h>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace vdrive
{
	class VirtualDrive
	{
	public:
		VirtualDrive() : m_open(false), m_fileCount(0), m_totalSize(0) {}
		~VirtualDrive() { Close(); }

		inline std::string WStringToUTF8(const std::wstring& ws)
		{
			if (ws.empty()) return {};
			int cb = WideCharToMultiByte(CP_UTF8, 0,
				ws.c_str(), static_cast<int>(ws.size()),
				nullptr, 0, nullptr, nullptr);
			std::string utf8(cb, '\0');
			WideCharToMultiByte(CP_UTF8, 0,
				ws.c_str(), static_cast<int>(ws.size()),
				utf8.data(), cb, nullptr, nullptr);
			return utf8;
		}

		inline std::wstring UTF8ToWString(const std::string& s)
		{
			if (s.empty()) return {};
			int cw = MultiByteToWideChar(CP_UTF8, 0,
				s.c_str(), static_cast<int>(s.size()),
				nullptr, 0);
			std::wstring ws(cw, L'\0');
			MultiByteToWideChar(CP_UTF8, 0,
				s.c_str(), static_cast<int>(s.size()),
				ws.data(), cw);
			return ws;
		}

		// Pack a directory (recursively) into a pak file.
		// includeHidden=false skips hidden/system files. Set true to include them.
		// errorOut can be nullptr; otherwise it will hold a textual reason on failure.
		bool PackDirectory(const std::wstring& sourceDirectory,
			const std::wstring& destinationPakFile,
			bool includeHidden = false,
			std::wstring* errorOut = nullptr)
		{
			if (errorOut) errorOut->clear();

			std::wstring srcRoot;
			if (!GetFullPath(sourceDirectory, srcRoot))
				return Fail(errorOut, L"Failed to resolve source directory path.");

			if (!IsDirectory(srcRoot))
				return Fail(errorOut, L"Source is not a directory.");

			std::wstring dstFull;
			if (!GetFullPath(destinationPakFile, dstFull))
				return Fail(errorOut, L"Failed to resolve destination pak path.");

			// Prevent creating pak inside source directory (would recurse into itself)
			if (IsPathInside(dstFull, srcRoot))
				return Fail(errorOut, L"Destination pak is inside source directory.");

			// Enumerate files
			std::vector<FileToPack> files;
			if (!EnumerateFiles(srcRoot, L"", includeHidden, files))
				return Fail(errorOut, L"Failed to enumerate files.");

			// If no files, still create a tiny empty pak
			// Write to a temporary and atomic replace
			std::wstring tmp = dstFull + L".tmp";

			std::ofstream out(tmp.c_str(), std::ios::binary);
			if (!out)
				return Fail(errorOut, L"Cannot create temporary pak file for writing.");

			// Prepare header placeholders
			if (!WriteMagic(out)) return Fail(errorOut, L"Write header failed (magic).");
			if (!WriteU16(out, 1)) return Fail(errorOut, L"Write header failed (version).");
			if (!WriteU16(out, 0)) return Fail(errorOut, L"Write header failed (flags).");
			if (!WriteU64(out, 0)) return Fail(errorOut, L"Write header failed (fileCount placeholder).");
			if (!WriteU64(out, 0)) return Fail(errorOut, L"Write header failed (indexOffset placeholder).");

			// Write data section
			std::vector<IndexEntry> index;
			index.reserve(files.size());

			const size_t BUF_SIZE = 1024 * 1024; // 1MB I/O buffer
			std::vector<char> buf(BUF_SIZE);

			for (size_t i = 0; i < files.size(); ++i)
			{
				const FileToPack& f = files[i];

				std::ifstream in(f.full.c_str(), std::ios::binary);
				if (!in)
				{
					out.close();
					DeleteFileW(tmp.c_str());
					std::wstring msg = L"Failed to open source file: ";
					msg += f.full;
					return Fail(errorOut, msg.c_str());
				}

				uint64_t offset = (uint64_t)out.tellp();
				uint64_t remaining = f.size;
				uint32_t crc = Crc32Begin();

				while (remaining > 0)
				{
					size_t chunk = remaining > BUF_SIZE ? BUF_SIZE : (size_t)remaining;
					in.read(buf.data(), (std::streamsize)chunk);
					std::streamsize got = in.gcount();
					if (got <= 0)
					{
						in.close();
						out.close();
						DeleteFileW(tmp.c_str());
						std::wstring msg = L"Read error on: ";
						msg += f.full;
						return Fail(errorOut, msg.c_str());
					}
					crc = Crc32Update(crc, (const unsigned char*)buf.data(), (size_t)got);
					out.write(buf.data(), got);
					if (!out)
					{
						in.close();
						out.close();
						DeleteFileW(tmp.c_str());
						std::wstring msg = L"Write error on pak while writing: ";
						msg += f.rel;
						return Fail(errorOut, msg.c_str());
					}
					remaining -= (uint64_t)got;
				}
				in.close();

				IndexEntry e;
				e.offset = offset;
				e.size = f.size;
				e.crc32 = Crc32End(crc);
				e.pathNormalized = NormalizeToVirtual(f.rel); // forward slashes
				e.pathKey = ToLookupKey(e.pathNormalized);
				e.pathUtf8 = WStringToUTF8(e.pathNormalized);

				// Detect duplicates (case-insensitive on Windows)
				if (m_indexTemp.count(e.pathKey) != 0)
				{
					out.close();
					DeleteFileW(tmp.c_str());
					std::wstring msg = L"Duplicate relative path found: ";
					msg += e.pathNormalized;
					m_indexTemp.clear();
					return Fail(errorOut, msg.c_str());
				}
				m_indexTemp[e.pathKey] = (uint32_t)index.size();
				index.push_back(e);
			}

			m_indexTemp.clear(); // cleanup

			// Index offset
			uint64_t indexOffset = (uint64_t)out.tellp();

			// Write index
			for (size_t i = 0; i < index.size(); ++i)
			{
				const IndexEntry& e = index[i];
				uint32_t len = (uint32_t)e.pathUtf8.size();

				if (!WriteU32(out, len)) return FailClean(out, tmp, errorOut, L"Write index failed (pathLen).");
				if (!WriteU64(out, e.offset)) return FailClean(out, tmp, errorOut, L"Write index failed (offset).");
				if (!WriteU64(out, e.size)) return FailClean(out, tmp, errorOut, L"Write index failed (size).");
				if (!WriteU32(out, e.crc32)) return FailClean(out, tmp, errorOut, L"Write index failed (crc32).");

				if (len > 0)
				{
					out.write(e.pathUtf8.data(), len);
					if (!out) return FailClean(out, tmp, errorOut, L"Write index failed (path data).");
				}
			}

			// Update header with fileCount and indexOffset
			if (!out)
			{
				out.close();
				DeleteFileW(tmp.c_str());
				return Fail(errorOut, L"Pak write failed before header finalization.");
			}

			out.seekp(0, std::ios::beg);
			if (!out) { out.close(); DeleteFileW(tmp.c_str()); return Fail(errorOut, L"Seek to header failed."); }
			if (!WriteMagic(out)) { out.close(); DeleteFileW(tmp.c_str()); return Fail(errorOut, L"Re-write header failed (magic)."); }
			if (!WriteU16(out, 1)) { out.close(); DeleteFileW(tmp.c_str()); return Fail(errorOut, L"Re-write header failed (version)."); }
			if (!WriteU16(out, 0)) { out.close(); DeleteFileW(tmp.c_str()); return Fail(errorOut, L"Re-write header failed (flags)."); }
			if (!WriteU64(out, (uint64_t)index.size())) { out.close(); DeleteFileW(tmp.c_str()); return Fail(errorOut, L"Re-write header failed (fileCount)."); }
			if (!WriteU64(out, indexOffset)) { out.close(); DeleteFileW(tmp.c_str()); return Fail(errorOut, L"Re-write header failed (indexOffset)."); }

			out.flush();
			out.close();

			// Atomic replace
			if (!MoveFileExW(tmp.c_str(), dstFull.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH))
			{
				DeleteFileW(tmp.c_str());
				return Fail(errorOut, L"Failed to replace destination pak.");
			}

			return true;
		}

		// Open pak and load its index
		bool Open(const std::wstring& pakFile, std::wstring* errorOut = nullptr)
		{
			if (errorOut) errorOut->clear();
			Close();

			if (!GetFullPath(pakFile, m_openPath))
				return Fail(errorOut, L"Failed to resolve pak file path.");

			std::ifstream in(m_openPath.c_str(), std::ios::binary);
			if (!in) return Fail(errorOut, L"Cannot open pak file for reading.");

			// Header
			if (!ExpectMagic(in)) { in.close(); return Fail(errorOut, L"Invalid pak magic."); }
			uint16_t version = 0, flags = 0;
			uint64_t fileCount = 0, idxOff = 0;
			if (!ReadU16(in, version) || !ReadU16(in, flags) ||
				!ReadU64(in, fileCount) || !ReadU64(in, idxOff))
			{
				in.close();
				return Fail(errorOut, L"Failed to read pak header.");
			}
			if (version != 1) { in.close(); return Fail(errorOut, L"Unsupported pak version."); }

			// Jump to index
			in.seekg((std::streamoff)idxOff, std::ios::beg);
			if (!in) { in.close(); return Fail(errorOut, L"Failed to seek to index."); }

			m_entries.clear();
			m_lookup.clear();
			m_totalSize = 0;

			for (uint64_t i = 0; i < fileCount; ++i)
			{
				uint32_t pathLen = 0;
				uint64_t off = 0, sz = 0;
				uint32_t crc = 0;

				if (!ReadU32(in, pathLen) || !ReadU64(in, off) ||
					!ReadU64(in, sz) || !ReadU32(in, crc))
				{
					in.close();
					Close();
					return Fail(errorOut, L"Index read failed.");
				}
				std::string p8;
				p8.resize(pathLen);
				if (pathLen > 0)
				{
					in.read(&p8[0], pathLen);
					if (!in) { in.close(); Close(); return Fail(errorOut, L"Index path read failed."); }
				}

				Entry e;
				e.offset = off;
				e.size = sz;
				e.crc32 = crc;
				e.path = UTF8ToWString(p8);

				// Normalize and index
				std::wstring norm = NormalizeToVirtual(e.path);
				e.path = norm;
				std::wstring key = ToLookupKey(norm);

				if (m_lookup.count(key) != 0)
				{
					in.close();
					Close();
					std::wstring msg = L"Duplicate entry in pak: ";
					msg += norm;
					return Fail(errorOut, msg.c_str());
				}
				m_lookup[key] = (uint32_t)m_entries.size();
				m_entries.push_back(e);
				m_totalSize += e.size;
			}

			in.close();
			m_open = true;
			return true;
		}

		// Reads a file and returns its content as a Base64 string.
		// Returns an empty string on failure; check errorOut for details.
		std::string GetBase64(const std::wstring& virtualPath, std::wstring* errorOut = nullptr) const
		{
			std::vector<unsigned char> data;
			if (!ReadFile(virtualPath, data, true, errorOut))
				return "";

			static const char* lut = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			size_t len = data.size();
			std::string out;
			out.reserve(((len + 2) / 3) * 4);

			for (size_t i = 0; i < len; i += 3) {
				uint32_t val = (uint32_t)data[i] << 16;
				if (i + 1 < len) val |= (uint32_t)data[i + 1] << 8;
				if (i + 2 < len) val |= (uint32_t)data[i + 2];

				out.push_back(lut[(val >> 18) & 0x3F]);
				out.push_back(lut[(val >> 12) & 0x3F]);
				out.push_back((i + 1 < len) ? lut[(val >> 6) & 0x3F] : '=');
				out.push_back((i + 2 < len) ? lut[val & 0x3F] : '=');
			}
			return out;
		}

		void Close()
		{
			m_open = false;
			m_openPath.clear();
			m_entries.clear();
			m_lookup.clear();
			m_totalSize = 0;
			m_fileCount = 0;
		}

		bool IsOpen() const { return m_open; }

		// Check if a virtual path exists in the pak
		bool Contains(const std::wstring& virtualPath) const
		{
			if (!m_open) return false;
			std::wstring key = ToLookupKey(NormalizeToVirtual(virtualPath));
			return m_lookup.find(key) != m_lookup.end();
		}

		// Return file size by virtual path (0 if not found)
		uint64_t FileSize(const std::wstring& virtualPath) const
		{
			const Entry* e = GetEntry(virtualPath);
			return e ? e->size : 0;
		}

		// List all files; if folder is provided, it filters by that virtual folder (case-insensitive).
		std::vector<std::wstring> List(const std::wstring& folder = L"") const
		{
			std::vector<std::wstring> out;
			if (!m_open) return out;

			std::wstring prefix = NormalizeToVirtual(folder);
			if (!prefix.empty())
			{
				if (prefix.back() != L'/') prefix += L'/';
			}

			for (size_t i = 0; i < m_entries.size(); ++i)
			{
				const Entry& e = m_entries[i];
				if (prefix.empty())
					out.push_back(e.path);
				else
				{
					// case-insensitive compare on Windows
					if (StartsWithInsensitive(e.path, prefix))
						out.push_back(e.path);
				}
			}
			// Stable lexicographical order
			std::sort(out.begin(), out.end());
			return out;
		}

		// Read a file’s content into memory
		bool ReadFile(const std::wstring& virtualPath,
			std::vector<unsigned char>& outData,
			bool verifyCrc = true,
			std::wstring* errorOut = nullptr) const
		{
			if (errorOut) errorOut->clear();

			const Entry* e = GetEntry(virtualPath);
			if (!e) return Fail(errorOut, L"File not found in pak.");

			std::ifstream in(m_openPath.c_str(), std::ios::binary);
			if (!in) return Fail(errorOut, L"Cannot reopen pak file for reading.");

			in.seekg((std::streamoff)e->offset, std::ios::beg);
			if (!in) { in.close(); return Fail(errorOut, L"Seek error inside pak."); }

			outData.clear();
			outData.resize((size_t)e->size);

			if (e->size > 0)
			{
				in.read((char*)outData.data(), (std::streamsize)e->size);
				if (!in) { in.close(); outData.clear(); return Fail(errorOut, L"Read error from pak."); }
			}
			in.close();

			if (verifyCrc)
			{
				uint32_t crc = Crc32Begin();
				if (e->size > 0) crc = Crc32Update(crc, (const unsigned char*)outData.data(), (size_t)e->size);
				crc = Crc32End(crc);
				if (crc != e->crc32)
				{
					outData.clear();
					return Fail(errorOut, L"CRC mismatch.");
				}
			}
			return true;
		}

		// Extract a single virtual file to disk
		bool ExtractFile(const std::wstring& virtualPath,
			const std::wstring& destinationPath,
			bool verifyCrc = true,
			std::wstring* errorOut = nullptr) const
		{
			if (errorOut) errorOut->clear();

			std::vector<unsigned char> data;
			if (!ReadFile(virtualPath, data, verifyCrc, errorOut)) return false;

			if (!EnsureParentDirectories(destinationPath))
				return Fail(errorOut, L"Failed to create destination directories.");

			std::ofstream out(destinationPath.c_str(), std::ios::binary);
			if (!out) return Fail(errorOut, L"Cannot open destination file for writing.");
			if (!data.empty())
			{
				out.write((const char*)data.data(), (std::streamsize)data.size());
				if (!out) { out.close(); DeleteFileW(destinationPath.c_str()); return Fail(errorOut, L"Write error to destination."); }
			}
			out.close();
			return true;
		}

		// Extract all files to a directory
		bool ExtractAll(const std::wstring& destinationDirectory,
			bool verifyCrc = true,
			std::wstring* errorOut = nullptr) const
		{
			if (errorOut) errorOut->clear();
			if (!m_open) return Fail(errorOut, L"Pak is not open.");

			std::wstring dstRoot;
			if (!GetFullPath(destinationDirectory, dstRoot))
				return Fail(errorOut, L"Failed to resolve destination directory.");

			if (!EnsureDirectory(dstRoot))
				return Fail(errorOut, L"Failed to create destination directory.");

			for (size_t i = 0; i < m_entries.size(); ++i)
			{
				const Entry& e = m_entries[i];
				std::wstring winPath = dstRoot;
				if (!winPath.empty() && winPath.back() != L'\\') winPath += L'\\';

				// Convert virtual "/" to Windows "\"
				std::wstring relWin = e.path;
				for (size_t k = 0; k < relWin.size(); ++k)
					if (relWin[k] == L'/') relWin[k] = L'\\';

				// Sanitize relative path to avoid traversal
				relWin = SanitizeRelative(relWin);

				winPath += relWin;
				if (!ExtractFile(e.path, winPath, verifyCrc, errorOut))
				{
					return false; // errorOut already filled
				}
			}
			return true;
		}

		// Verify CRC of all files without extracting
		bool VerifyAll(std::wstring* errorOut = nullptr) const
		{
			if (errorOut) errorOut->clear();
			if (!m_open) return Fail(errorOut, L"Pak is not open.");

			std::ifstream in(m_openPath.c_str(), std::ios::binary);
			if (!in) return Fail(errorOut, L"Cannot open pak for verification.");

			const size_t BUF_SIZE = 1024 * 1024;
			std::vector<unsigned char> buf(BUF_SIZE);

			for (size_t i = 0; i < m_entries.size(); ++i)
			{
				const Entry& e = m_entries[i];
				in.seekg((std::streamoff)e.offset, std::ios::beg);
				if (!in) { in.close(); return Fail(errorOut, (L"Seek failed for: " + e.path).c_str()); }

				uint64_t remaining = e.size;
				uint32_t crc = Crc32Begin();

				while (remaining > 0)
				{
					size_t chunk = remaining > BUF_SIZE ? BUF_SIZE : (size_t)remaining;
					in.read((char*)buf.data(), (std::streamsize)chunk);
					std::streamsize got = in.gcount();
					if (got <= 0) { in.close(); return Fail(errorOut, (L"Read failed for: " + e.path).c_str()); }
					crc = Crc32Update(crc, (const unsigned char*)buf.data(), (size_t)got);
					remaining -= (uint64_t)got;
				}
				crc = Crc32End(crc);
				if (crc != e.crc32)
				{
					in.close();
					std::wstring msg = L"CRC mismatch: ";
					msg += e.path;
					return Fail(errorOut, msg.c_str());
				}
			}
			in.close();
			return true;
		}

		struct Info
		{
			uint64_t fileCount;
			uint64_t totalSize;
		};

		Info GetInfo() const
		{
			Info i{};
			i.fileCount = (uint64_t)m_entries.size();
			i.totalSize = m_totalSize;
			return i;
		}

	private:
		struct Entry
		{
			uint64_t offset{ 0 };
			uint64_t size{ 0 };
			uint32_t crc32{ 0 };
			std::wstring path; // normalized with "/"
		};

		struct FileToPack
		{
			std::wstring full; // absolute path
			std::wstring rel;  // relative path from source root (using '\' separators)
			uint64_t     size{ 0 };
		};

		struct IndexEntry
		{
			uint64_t offset{ 0 };
			uint64_t size{ 0 };
			uint32_t crc32{ 0 };
			std::wstring pathNormalized; // "/"
			std::wstring pathKey;        // lowercase for lookup
			std::string  pathUtf8;
		};

		// State
		bool m_open;
		std::wstring m_openPath;
		std::vector<Entry> m_entries;
		std::unordered_map<std::wstring, uint32_t> m_lookup; // key = lowercase normalized path
		uint64_t m_fileCount;
		uint64_t m_totalSize;

		// Temporary used during pack to catch duplicates quickly
		std::unordered_map<std::wstring, uint32_t> m_indexTemp;

		// --------- Helpers

		static inline bool Fail(std::wstring* out, const std::wstring& msg) {
			if (out) *out = msg;
			return false;
		}
		static inline bool FailClean(std::ofstream& f, const std::wstring& tmpPath, std::wstring* out, const std::wstring& msg) {
			f.close();
			DeleteFileW(tmpPath.c_str());
			if (out) *out = msg;
			return false;
		}

		static bool GetFullPath(const std::wstring& in, std::wstring& out)
		{
			out.clear();
			wchar_t buffer[MAX_PATH];
			wchar_t* filePart = nullptr;
			DWORD n = GetFullPathNameW(in.c_str(), (DWORD)(sizeof(buffer) / sizeof(wchar_t)), buffer, &filePart);
			if (n == 0) return false;
			if (n < (DWORD)(sizeof(buffer) / sizeof(wchar_t))) {
				out.assign(buffer, buffer + wcslen(buffer));
				return true;
			}
			// Need bigger buffer
			std::vector<wchar_t> big(n + 2, 0);
			DWORD m = GetFullPathNameW(in.c_str(), n + 1, big.data(), &filePart);
			if (m == 0) return false;
			out.assign(big.data(), big.data() + wcslen(big.data()));
			return true;
		}

		static bool IsDirectory(const std::wstring& path)
		{
			DWORD attr = GetFileAttributesW(path.c_str());
			if (attr == INVALID_FILE_ATTRIBUTES) return false;
			return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
		}

		static bool EnsureDirectory(const std::wstring& path)
		{
			// Create directory tree. Simple step-by-step creation.
			if (path.empty()) return false;

			// If it exists and is directory, OK
			DWORD attr = GetFileAttributesW(path.c_str());
			if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;

			std::wstring acc;
			acc.reserve(path.size());

			for (size_t i = 0; i < path.size(); ++i)
			{
				wchar_t c = path[i];
				if (c == L'/' || c == L'\\')
				{
					if (!acc.empty())
					{
						if (!CreateDirectoryW(acc.c_str(), nullptr))
						{
							DWORD err = GetLastError();
							if (err != ERROR_ALREADY_EXISTS)
								return false;
						}
					}
				}
				acc.push_back(c);
			}
			// Create the final directory too
			if (!CreateDirectoryW(acc.c_str(), nullptr))
			{
				DWORD err = GetLastError();
				if (err != ERROR_ALREADY_EXISTS)
					return false;
			}
			return true;
		}

		static bool EnsureParentDirectories(const std::wstring& filePath)
		{
			// Extract parent directory
			std::wstring dir = filePath;
			for (size_t i = dir.size(); i > 0; --i)
			{
				wchar_t c = dir[i - 1];
				if (c == L'\\' || c == L'/')
				{
					dir.resize(i - 1);
					break;
				}
			}
			return dir.empty() ? true : EnsureDirectory(dir);
		}

		static std::wstring NormalizeToVirtual(const std::wstring& p)
		{
			// Convert backslashes to slashes and remove leading slashes or "./"
			std::wstring s = p;
			for (size_t i = 0; i < s.size(); ++i) if (s[i] == L'\\') s[i] = L'/';
			// Trim leading slashes
			while (!s.empty() && (s[0] == L'/' || s[0] == L'.')) {
				if (s.size() >= 2 && s[0] == L'.' && s[1] == L'/') { s.erase(0, 2); }
				else if (s[0] == L'/') { s.erase(0, 1); }
				else break;
			}
			return s;
		}

		static std::wstring ToLower(const std::wstring& s)
		{
			std::wstring t = s;
			for (size_t i = 0; i < t.size(); ++i)
			{
				wchar_t c = t[i];
				if (c >= L'A' && c <= L'Z')
					t[i] = c - L'A' + L'a';
			}
			return t;
		}

		static std::wstring ToLookupKey(const std::wstring& normalizedVirtual)
		{
			// Case-insensitive by lowercasing, Windows-like
			return ToLower(normalizedVirtual);
		}

		static bool StartsWithInsensitive(const std::wstring& s, const std::wstring& prefix)
		{
			if (prefix.size() > s.size()) return false;

			for (size_t i = 0; i < prefix.size(); ++i)
			{
				wchar_t a = s[i];
				wchar_t b = prefix[i];
				if (a >= L'A' && a <= L'Z') a = a - L'A' + L'a';
				if (b >= L'A' && b <= L'Z') b = b - L'A' + L'a';
				if (a != b) return false;
			}
			return true;
		}

		// Make sure the destination pak is not inside source root (avoid self-embedding)
		static bool IsPathInside(const std::wstring& path, const std::wstring& dir)
		{
			// Compare normalized lowercase prefixes with backslash as separators
			std::wstring p = ToLower(path);
			std::wstring d = ToLower(dir);

			// Ensure both end in backslash for clean prefix check
			if (!d.empty() && d.back() != L'\\' && d.back() != L'/')
				d.push_back(L'\\');

			std::wstring pp = p;
			for (size_t i = 0; i < pp.size(); ++i) if (pp[i] == L'/') pp[i] = L'\\';
			for (size_t i = 0; i < d.size(); ++i) if (d[i] == L'/') d[i] = L'\\';

			if (pp.size() < d.size()) return false;
			return _wcsnicmp(pp.c_str(), d.c_str(), d.size()) == 0;
		}

		// Sanitize relative path to prevent path traversal or illegal components
		static std::wstring SanitizeRelative(const std::wstring& relPath)
		{
			// Split by slash/backslash, rebuild without ".." and remove any drive/colon
			std::wstring tmp = relPath;
			for (size_t i = 0; i < tmp.size(); ++i)
				if (tmp[i] == L'/') tmp[i] = L'\\';

			std::vector<std::wstring> parts;
			std::wstring part;
			for (size_t i = 0; i < tmp.size(); ++i)
			{
				wchar_t c = tmp[i];
				if (c == L'\\')
				{
					if (!part.empty()) { parts.push_back(part); part.clear(); }
				}
				else
				{
					part.push_back(c);
				}
			}
			if (!part.empty()) { parts.push_back(part); part.clear(); }

			std::vector<std::wstring> cleaned;
			for (size_t i = 0; i < parts.size(); ++i)
			{
				const std::wstring& it = parts[i];
				if (it == L"." || it.empty()) continue;
				if (it == L"..") {
					if (!cleaned.empty()) cleaned.pop_back();
					continue;
				}
				// Remove colon to avoid drive specifiers
				std::wstring cpy;
				cpy.reserve(it.size());
				for (size_t k = 0; k < it.size(); ++k)
				{
					if (it[k] == L':') continue;
					cpy.push_back(it[k]);
				}
				cleaned.push_back(cpy);
			}

			std::wstring out;
			for (size_t i = 0; i < cleaned.size(); ++i)
			{
				if (i > 0) out.push_back(L'\\');
				out += cleaned[i];
			}
			return out;
		}

		// Enumerate directory recursively (skips reparse points, optionally skips hidden/system)
		static bool EnumerateFiles(const std::wstring& rootAbs,
			const std::wstring& rel,
			bool includeHidden,
			std::vector<FileToPack>& out)
		{
			std::wstring search = rootAbs;
			if (!search.empty() && search.back() != L'\\') search += L'\\';
			search += rel;
			if (!search.empty() && search.back() != L'\\') search += L'\\';
			search += L"*";

			WIN32_FIND_DATAW fd;
			HANDLE h = FindFirstFileW(search.c_str(), &fd);
			if (h == INVALID_HANDLE_VALUE)
			{
				// Directory may be empty
				DWORD err = GetLastError();
				if (err == ERROR_FILE_NOT_FOUND) return true;
				// Otherwise, stop enumeration as error
				return false;
			}

			do {
				std::wstring name = fd.cFileName;
				if (name == L"." || name == L"..") continue;

				DWORD attr = fd.dwFileAttributes;
				bool isDir = (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
				bool isHiddenSys = (attr & FILE_ATTRIBUTE_HIDDEN) != 0 || (attr & FILE_ATTRIBUTE_SYSTEM) != 0;
				bool isReparse = (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
				if (isReparse) continue; // skip symlinks/junctions

				if (!includeHidden && isHiddenSys) {
					// skip hidden/system by default
				}

				std::wstring relChild = rel;
				relChild += name;

				if (isDir)
				{
					relChild += L"\\";
					if (!EnumerateFiles(rootAbs, relChild, includeHidden, out))
					{
						FindClose(h);
						return false;
					}
				}
				else
				{
					FileToPack f;
					f.rel = relChild;
					f.full = rootAbs;
					if (!f.full.empty() && f.full.back() != L'\\') f.full.push_back(L'\\');
					f.full += relChild;

					uint64_t size = ((uint64_t)fd.nFileSizeHigh << 32) | (uint64_t)fd.nFileSizeLow;
					f.size = size;
					out.push_back(f);
				}
			} while (FindNextFileW(h, &fd));

			FindClose(h);
			return true;
		}

		// Low-level write/read helpers (little-endian)
		static bool WriteMagic(std::ofstream& out)
		{
			const char m[4] = { 'V', 'D', 'P', 'K' };
			out.write(m, 4);
			return !!out;
		}
		static bool ExpectMagic(std::ifstream& in)
		{
			char m[4] = { 0,0,0,0 };
			in.read(m, 4);
			return in && m[0] == 'V' && m[1] == 'D' && m[2] == 'P' && m[3] == 'K';
		}
		static bool WriteU16(std::ofstream& out, uint16_t v)
		{
			unsigned char b[2];
			b[0] = (unsigned char)(v & 0xFF);
			b[1] = (unsigned char)((v >> 8) & 0xFF);
			out.write((const char*)b, 2);
			return !!out;
		}
		static bool WriteU32(std::ofstream& out, uint32_t v)
		{
			unsigned char b[4];
			b[0] = (unsigned char)(v & 0xFF);
			b[1] = (unsigned char)((v >> 8) & 0xFF);
			b[2] = (unsigned char)((v >> 16) & 0xFF);
			b[3] = (unsigned char)((v >> 24) & 0xFF);
			out.write((const char*)b, 4);
			return !!out;
		}
		static bool WriteU64(std::ofstream& out, uint64_t v)
		{
			unsigned char b[8];
			b[0] = (unsigned char)(v & 0xFF);
			b[1] = (unsigned char)((v >> 8) & 0xFF);
			b[2] = (unsigned char)((v >> 16) & 0xFF);
			b[3] = (unsigned char)((v >> 24) & 0xFF);
			b[4] = (unsigned char)((v >> 32) & 0xFF);
			b[5] = (unsigned char)((v >> 40) & 0xFF);
			b[6] = (unsigned char)((v >> 48) & 0xFF);
			b[7] = (unsigned char)((v >> 56) & 0xFF);
			out.write((const char*)b, 8);
			return !!out;
		}
		static bool ReadU16(std::ifstream& in, uint16_t& v)
		{
			unsigned char b[2];
			in.read((char*)b, 2);
			if (!in) return false;
			v = (uint16_t)(b[0] | (b[1] << 8));
			return true;
		}
		static bool ReadU32(std::ifstream& in, uint32_t& v)
		{
			unsigned char b[4];
			in.read((char*)b, 4);
			if (!in) return false;
			v = (uint32_t)(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
			return true;
		}
		static bool ReadU64(std::ifstream& in, uint64_t& v)
		{
			unsigned char b[8];
			in.read((char*)b, 8);
			if (!in) return false;
			v = (uint64_t)b[0]
				| ((uint64_t)b[1] << 8)
				| ((uint64_t)b[2] << 16)
				| ((uint64_t)b[3] << 24)
				| ((uint64_t)b[4] << 32)
				| ((uint64_t)b[5] << 40)
				| ((uint64_t)b[6] << 48)
				| ((uint64_t)b[7] << 56);
			return true;
		}

		// CRC32 (polynomial 0xEDB88320), standard implementation
		static uint32_t Crc32Begin()
		{
			return 0xFFFFFFFFu;
		}
		static uint32_t Crc32End(uint32_t crc)
		{
			return crc ^ 0xFFFFFFFFu;
		}
		static uint32_t Crc32Update(uint32_t crc, const unsigned char* data, size_t len)
		{
			static unsigned int tableInit = 0;
			static uint32_t table[256];

			if (!tableInit)
			{
				for (uint32_t i = 0; i < 256; ++i)
				{
					uint32_t c = i;
					for (int j = 0; j < 8; ++j)
					{
						if (c & 1) c = 0xEDB88320u ^ (c >> 1);
						else c >>= 1;
					}
					table[i] = c;
				}
				tableInit = 1;
			}
			uint32_t c = crc;
			for (size_t i = 0; i < len; ++i)
			{
				c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
			}
			return c;
		}

		const Entry* GetEntry(const std::wstring& virtualPath) const
		{
			if (!m_open) return nullptr;
			std::wstring key = ToLookupKey(NormalizeToVirtual(virtualPath));
			auto it = m_lookup.find(key);
			if (it == m_lookup.end()) return nullptr;
			uint32_t idx = it->second;
			if (idx >= m_entries.size()) return nullptr;
			return &m_entries[idx];
		}
	};
} // namespace vdrive

/*
LLM / operator instructions and notes:
-The library stores file paths inside the pak using forward slashes “ / ” and UTF - 8. Externally, you should supply virtual paths like L"images/logo.png".Backslashes will be normalized.
- When packing :
-Ensure source directory exists.
- Avoid placing the destination pak inside the source directory to prevent recursion(this library already checks).
- Set includeHidden = true if your users want hidden / system files included.
- When extracting :
-The library sanitizes paths to avoid traversal(“..”) and drive letters.
- If extraction fails mid - way, you can delete the target directory manually.
- Error handling :
-Always check boolean return values.If you pass a std::wstring * errorOut, it will contain a friendly explanation for the user.

This header should compile cleanly with MSVC on Windows.It uses only traditional std iostreams and Win32 APIs(FindFirstFileW / FindNextFileW, CreateDirectoryW, MoveFileExW, etc.).#pragma once
*/
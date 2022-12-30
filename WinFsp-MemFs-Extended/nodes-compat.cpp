#include "exceptions.h"
#include "nodes.h"

namespace Memfs {
	static NTSTATUS CompatFspFileNodeSetEa(FSP_FILE_SYSTEM* fileSystem, PVOID fileNode, PFILE_FULL_EA_INFORMATION ea) {
		FileNode* node = static_cast<FileNode*>(fileNode);

		try {
			node->SetEa(ea);
		} catch (CreateException& ex) {
			return ex.Which();
		}

		return STATUS_SUCCESS;
	}
}

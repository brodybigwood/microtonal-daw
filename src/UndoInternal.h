#pragma once
#include <vector>

class Project;
class NodeManager;

/**
 * Shared by the UndoManager implementation files (UndoManager.cpp, UndoActions*.cpp,
 * UndoRegistry.cpp, UndoSerialization.cpp). Not part of the public undo API.
 *
 * Resolves the NodeManager addressed by `path` (patcher/multiplexer ids) starting
 * from the thread-active graph copy set via NodeProcessor::setThreadActiveRoot.
 */
NodeManager& requireManager(Project* p, const std::vector<int>& path);

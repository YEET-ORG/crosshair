/**************************************************************************/
/*  yeet_ai_context_tools.cpp                                             */
/**************************************************************************/
/*  YeetAI tool handlers for project context indexing and retrieval.       */
/**************************************************************************/

#include "yeet_ai_dock.h"
#include "yeet_ai_project_context_index.h"

Dictionary YeetAIDock::_tool_index_project_context(const Dictionary &p_args) const {
	YeetAIProjectContextIndex *index = YeetAIProjectContextIndex::get_singleton();
	if (index == nullptr) {
		YeetAIProjectContextIndex::initialize();
		index = YeetAIProjectContextIndex::get_singleton();
	}
	return index->rebuild_project_context(p_args);
}

Dictionary YeetAIDock::_tool_search_project_context(const Dictionary &p_args) const {
	YeetAIProjectContextIndex *index = YeetAIProjectContextIndex::get_singleton();
	if (index == nullptr) {
		YeetAIProjectContextIndex::initialize();
		index = YeetAIProjectContextIndex::get_singleton();
	}

	Dictionary result = index->search_project_context(p_args);
	if (bool(result.get("ok", false)) && int(result.get("total_chunks", 0)) == 0) {
		Dictionary options;
		index->rebuild_project_context(options);
		result = index->search_project_context(p_args);
	}
	return result;
}

//
// Created by refac on 04/11/2026.
//

export module misc_constants;


export namespace misc_constants {


constexpr auto ICONITEMBORDER = 5;

constexpr auto GITLOG_START = 0;
constexpr auto GITLOG_END = 100;
constexpr auto GITLOG_FAILED = 101;

constexpr auto LOGFILTER_NONE = 0;
constexpr auto LOGFILTER_ALL = 0xFFFF;
constexpr auto LOGFILTER_TOGGLE = 0x8000;
constexpr auto LOGFILTER_MESSAGES = 0x0001;
constexpr auto LOGFILTER_PATHS = 0x0002;
constexpr auto LOGFILTER_AUTHORS = 0x0004;
constexpr auto LOGFILTER_REVS = 0x0008;
constexpr auto LOGFILTER_REGEX = 0x0010;
constexpr auto LOGFILTER_BUGID = 0x0020;
constexpr auto LOGFILTER_SUBJECT = 0x0040;
constexpr auto LOGFILTER_REFNAME = 0x0080;
constexpr auto LOGFILTER_EMAILS = 0x0100;
constexpr auto LOGFILTER_NOTES = 0x0200;
constexpr auto LOGFILTER_ANNOTATEDTAG = 0x0400;
constexpr auto LOGFILTER_CASE = 0x0800;

constexpr auto LOGLIST_SHOWNOTHING = 0x0000;
constexpr auto LOGLIST_SHOWLOCALBRANCHES = 0x0001;
constexpr auto LOGLIST_SHOWREMOTEBRANCHES = 0x0002;
constexpr auto LOGLIST_SHOWTAGS = 0x0004;
constexpr auto LOGLIST_SHOWSTASH = 0x0008;
constexpr auto LOGLIST_SHOWBISECT = 0x0010;
constexpr auto LOGLIST_SHOWOTHERREFS = 0x0020;
constexpr auto LOGLIST_SHOWALLREFS = 0xFFFF;

//typedef void CALLBACK_PROCESS(void * data, int progress);
constexpr auto MSG_LOADED = (WM_USER+110);
constexpr auto MSG_LOAD_PERCENTAGE = (WM_USER+111);
constexpr auto MSG_REFLOG_CHANGED = (WM_USER+112);
constexpr auto MSG_FETCHED_DIFF = (WM_USER+113);
constexpr auto MSG_COMMITS_REORDERED = (WM_USER+114);


constexpr auto LOG_INDEX_MAGIC = 0x88AA5566;
constexpr auto LOG_DATA_MAGIC = 0x99BB0FFF;
constexpr auto LOG_DATA_ITEM_MAGIC = 0x0FCC9ACC;
constexpr auto LOG_DATA_FILE_MAGIC = 0x19EE9DFF;
// bump whenever SLogCacheIndexItem's on-disk layout changes size, e.g. CGitHash/git_oid
// growing under GIT_EXPERIMENTAL_SHA256 (33 bytes vs. 20) even while static_cast<int>(tgit::invarients::GitFormatHolder::GitHashSize()) stays 20
constexpr auto LOG_INDEX_VERSION = 0x12;
constexpr auto INDEX_FILE_NAME = L"tortoisegit.index";
constexpr auto DATA_FILE_NAME = L"tortoisegit.data";
constexpr auto HISTORYLENGTH = 50;


}
export using namespace misc_constants;

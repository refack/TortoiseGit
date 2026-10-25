// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2026 - TortoiseGit

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//

// The line framer CGitCall feeds its receivers through. It is fed a buffer that
// is still growing, so the edges worth pinning are the ones a whole-string
// tokenizer never meets: an unterminated tail, a terminator split across two
// reads, and resuming from an offset. Only str_utils is imported, so this file
// compiles without the rest of the core library.

#include "stdafx.h"
using namespace std::literals;
using testing::ElementsAre;
using testing::IsEmpty;

namespace {

using Lines = std::vector<std::string>;

struct Framed {
	Lines lines;
	std::vector<std::size_t> ends; // the offset yielded with each line
};


Framed GitLines(const std::string_view buf, const std::size_t from = 0) {
	Framed out;
	for (const auto& [line, next] : tgit::str::lazy_tokenize_lines(buf, from)) {
		out.lines.emplace_back(line);
		out.ends.push_back(next);
	}
	return out;
}


/// Feeds \a chunks the way CGitCall::DeliverCompleteLines does: append a read,
/// deliver what is complete, erase what was delivered.
Lines StreamGitLines(const std::initializer_list<std::string_view> chunks, std::string* leftover = nullptr) {
	Lines lines;
	std::string pending;
	for (const auto chunk : chunks) {
		pending += chunk;
		std::size_t consumed = 0;
		for (const auto& [line, next] : tgit::str::lazy_tokenize_lines(pending)) {
			lines.emplace_back(line);
			consumed = next;
		}
		pending.erase(0, consumed);
	}
	if (leftover)
		*leftover = pending;
	return lines;
}

}



// ---- generate_git_lines: whole buffers ---------------------------------------------

TEST(GitLines, EmptyBufferYieldsNothing)
{
	const auto r = GitLines("");
	EXPECT_THAT(r.lines, IsEmpty());
}

TEST(GitLines, UnterminatedTailIsNotYielded)
{
	// it used to read buf.at(npos - 1) here and throw
	EXPECT_THAT(GitLines("tail").lines, IsEmpty());

	const auto r = GitLines("a\nta");
	EXPECT_THAT(r.lines, ElementsAre("a"));
	EXPECT_THAT(r.ends, ElementsAre(2u)); // the tail stays unconsumed, so the next read can finish it
}

TEST(GitLines, EveryLineIsYieldedWithTheOffsetPastItsNewline)
{
	// std::max(nl + 1, size) used to jump to the end after the first line
	const auto r = GitLines("a\nbb\nccc\n");
	EXPECT_THAT(r.lines, ElementsAre("a", "bb", "ccc"));
	EXPECT_THAT(r.ends, ElementsAre(2u, 5u, 9u));
}

TEST(GitLines, CrLfDropsTheCr)
{
	const auto r = GitLines("a\r\nb\r\n");
	EXPECT_THAT(r.lines, ElementsAre("a", "b"));
	EXPECT_THAT(r.ends, ElementsAre(3u, 6u));
}

TEST(GitLines, ACrNotBeforeTheNewlineIsData)
{
	EXPECT_THAT(GitLines("a\rb\n").lines, ElementsAre("a\rb"));
	EXPECT_THAT(GitLines("\ra\n").lines, ElementsAre("\ra"));
	EXPECT_THAT(GitLines("a\r\r\n").lines, ElementsAre("a\r")); // only the one right before '\n'
}

TEST(GitLines, EmptyLinesAreLines)
{
	EXPECT_THAT(GitLines("\n").lines, ElementsAre(""));
	EXPECT_THAT(GitLines("\n\n").lines, ElementsAre("", ""));
	EXPECT_THAT(GitLines("\r\n").lines, ElementsAre(""));
	EXPECT_THAT(GitLines("a\n\nb\n").lines, ElementsAre("a", "", "b"));
}



// ---- generate_git_lines: resuming from an offset -----------------------------------

TEST(GitLines, ResumesFromTheGivenOffset)
{
	const auto r = GitLines("a\nb\n", 2);
	EXPECT_THAT(r.lines, ElementsAre("b"));
	EXPECT_THAT(r.ends, ElementsAre(4u));
}

TEST(GitLines, ResumingAtTheEndYieldsNothing)
{
	EXPECT_THAT(GitLines("a\n", 2).lines, IsEmpty());
}

TEST(GitLines, ResumingBetweenCrAndLfDoesNotUnderflow)
{
	// The generator never hands out this offset itself, but the parameter allows it.
	// nl >= 1 used to look at the '\r' before start, and nl - start - 1 wrapped to npos.
	const auto r = GitLines("x\r\ny\n", 2);
	EXPECT_THAT(r.lines, ElementsAre("", "y"));
	EXPECT_THAT(r.ends, ElementsAre(3u, 5u));
}

TEST(GitLines, StoppingEarlyLeavesTheRestUnconsumed)
{
	std::size_t consumed = 0;
	for (const auto& [line, next] : tgit::str::lazy_tokenize_lines("a\nb\nc\n"s)) {
		consumed = next;
		if (line == "b")
			break;
	}
	EXPECT_EQ(4u, consumed);
}



// ---- generate_git_lines: streamed, as CGitCall feeds it ---------------------------

TEST(GitLines, StreamedTailIsCompletedByTheNextRead)
{
	std::string leftover;
	EXPECT_THAT(StreamGitLines({ "a\nb", "b\nc" }, &leftover), ElementsAre("a", "bb"));
	EXPECT_EQ("c", leftover);
}

TEST(GitLines, StreamedCrLfSplitAcrossReadsIsOneTerminator)
{
	EXPECT_THAT(StreamGitLines({ "a\r", "\nb\n" }), ElementsAre("a", "b"));
}

TEST(GitLines, EverySplitPointFramesLikeTheWholeBuffer)
{
	constexpr std::string_view whole = "ab\r\n\ncd\r\n\r\nef\rg\nh";
	std::string wholeLeftover;
	const Lines expected = StreamGitLines({ whole }, &wholeLeftover);
	ASSERT_THAT(expected, ElementsAre("ab", "", "cd", "", "ef\rg"));
	ASSERT_EQ("h", wholeLeftover);

	for (std::size_t k = 0; k <= whole.size(); ++k) {
		std::string leftover;
		EXPECT_EQ(expected, StreamGitLines({ whole.substr(0, k), whole.substr(k) }, &leftover)) << "split at " << k;
		EXPECT_EQ(wholeLeftover, leftover) << "split at " << k;
	}
}

TEST(GitLines, ByteAtATimeFramesLikeTheWholeBuffer)
{
	constexpr std::string_view whole = "ab\r\n\ncd\r\n\r\nef\rg\nh";
	std::string pending;
	Lines lines;
	for (const char c : whole) {
		pending += c;
		std::size_t consumed = 0;
		for (const auto& [line, next] : tgit::str::lazy_tokenize_lines(pending)) {
			lines.emplace_back(line);
			consumed = next;
		}
		pending.erase(0, consumed);
	}
	EXPECT_EQ(StreamGitLines({ whole }), lines);
	EXPECT_EQ("h", pending);
}

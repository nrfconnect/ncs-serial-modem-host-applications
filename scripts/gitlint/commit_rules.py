# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Gitlint rules for this repository (single source of truth).

Commit titles combine Conventional Commits semver types with Zephyr-style
subsystem prefixes. See docs/ci-and-contribution.md.
"""

from __future__ import annotations

import re

from gitlint.options import IntOption, StrOption
from gitlint.rules import (
    CommitMessageBody,
    CommitMessageTitle,
    CommitRule,
    LineRule,
    RuleViolation,
)

CONVENTIONAL_TYPES = (
    "feat",
    "fix",
    "docs",
    "style",
    "refactor",
    "perf",
    "test",
    "build",
    "ci",
    "chore",
)

DEFAULT_TITLE_REGEX = (
    r"^(feat|fix|docs|style|refactor|perf|test|build|ci|chore)(!)?: "
    r"(?!subsys:)(([^:]+):)(\s([^:]+):)*\s(.+)$"
)


class TitleConventionalSemverZephyr(LineRule):
    """Require `<type>[!]: <subsystem>: [<component>:] <description>`."""

    name = "title-conventional-semver-zephyr"
    id = "UC10"
    target = CommitMessageTitle
    options_spec = [
        StrOption(
            "regex",
            DEFAULT_TITLE_REGEX,
            "Regex the combined semver + Zephyr title must match",
        )
    ]

    def validate(self, title, _commit):
        regex = self.options["regex"].value
        pattern = re.compile(regex, re.UNICODE)
        allowed = ", ".join(CONVENTIONAL_TYPES)
        message = (
            "Commit title must follow `<type>[!]: <subsystem>: [<component>:] "
            f"<description>` (types: {allowed})"
        )
        if not pattern.search(title):
            return [RuleViolation(self.id, message, title)]


class BreakingChangeFooter(CommitRule):
    """Require a BREAKING CHANGE footer when the title uses the `!` marker."""

    name = "breaking-change-footer"
    id = "UC11"

    def validate(self, commit):
        title = commit.message.title
        if not re.match(
            r"^(feat|fix|docs|style|refactor|perf|test|build|ci|chore)!:",
            title,
        ):
            return

        for line in commit.message.body:
            if line.startswith("BREAKING CHANGE:"):
                return

        return [
            RuleViolation(
                self.id,
                "Commit titles with `!` must include a `BREAKING CHANGE:` footer line",
                line_nr=1,
            )
        ]


class BodyMinLineCount(CommitRule):
    name = "body-min-line-count"
    id = "UC6"
    options_spec = [
        IntOption(
            "min-line-count",
            1,
            "Minimum body line count excluding Signed-off-by",
        )
    ]

    def validate(self, commit):
        filtered = [
            line
            for line in commit.message.body
            if not line.lower().startswith("signed-off-by") and line != ""
        ]
        min_line_count = self.options["min-line-count"].value
        if len(filtered) < min_line_count:
            return [
                RuleViolation(
                    self.id,
                    f"Commit message body is empty, should at least have "
                    f"{min_line_count} line(s).",
                    line_nr=1,
                )
            ]


class BodyMaxLineCount(CommitRule):
    name = "body-max-line-count"
    id = "UC1"
    options_spec = [IntOption("max-line-count", 200, "Maximum body line count")]

    def validate(self, commit):
        line_count = len(commit.message.body)
        max_line_count = self.options["max-line-count"].value
        if line_count > max_line_count:
            return [
                RuleViolation(
                    self.id,
                    f"Commit message body contains too many lines "
                    f"({line_count} > {max_line_count})",
                    line_nr=1,
                )
            ]


class SignedOffBy(CommitRule):
    name = "body-requires-signed-off-by"
    id = "UC2"

    def validate(self, commit):
        flags = re.UNICODE | re.IGNORECASE
        for line in commit.message.body:
            if line.lower().startswith("signed-off-by"):
                if not re.search(
                    r"(^)Signed-off-by: ([-'\w.]+) ([-'\w.]+) (.*)",
                    line,
                    flags=flags,
                ):
                    return [
                        RuleViolation(
                            self.id,
                            "Signed-off-by: must have a full name",
                            line_nr=1,
                        )
                    ]
                return
        return [
            RuleViolation(
                self.id,
                "Commit message does not contain a 'Signed-off-by:' line",
                line_nr=1,
            )
        ]


class TitleMaxLengthRevert(LineRule):
    name = "title-max-length-no-revert"
    id = "UC5"
    target = CommitMessageTitle
    options_spec = [IntOption("line-length", 120, "Max line length")]
    violation_message = "Commit title exceeds max length ({0}>{1})"

    def validate(self, line, _commit):
        max_length = self.options["line-length"].value
        if len(line) > max_length and not line.startswith("Revert"):
            return [
                RuleViolation(
                    self.id,
                    self.violation_message.format(len(line), max_length),
                    line,
                )
            ]


class MaxLineLengthExceptions(LineRule):
    name = "max-line-length-with-exceptions"
    id = "UC4"
    target = CommitMessageBody
    options_spec = [IntOption("line-length", 120, "Max line length")]
    violation_message = "Commit message body line exceeds max length ({0}>{1})"

    def validate(self, line, _commit):
        max_length = self.options["line-length"].value
        urls = re.findall(
            r"http[s]?://(?:[a-zA-Z]|[0-9]|[$_.@&+-]|[!*\(\),]|(?:%[0-9a-fA-F][0-9a-fA-F]))+",
            line,
        )
        if line.lower().startswith("signed-off-by") or line.lower().startswith(
            "co-authored-by"
        ):
            return

        if urls:
            return

        if len(line) > max_length:
            return [
                RuleViolation(
                    self.id,
                    self.violation_message.format(len(line), max_length),
                    line,
                )
            ]


class BodyContainsBlockedTags(LineRule):
    name = "body-contains-blocked-tags"
    id = "UC7"
    target = CommitMessageBody
    tags = ["Change-Id"]

    def validate(self, line, _commit):
        flags = re.IGNORECASE
        for tag in self.tags:
            if re.search(rf"^\s*{tag}:", line, flags=flags):
                return [
                    RuleViolation(
                        self.id,
                        f"Commit message contains a blocked tag: {tag}",
                    )
                ]
        return

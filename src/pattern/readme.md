# Augmented Backus-Naur Form Pattern Matching

An implementation of generic pattern matching and an Augmented BNF compiler. The pattern matching is implemented as a finite state machine in ``match.c``, fully capable of capturing tokens and recursive backtracking. ``augbnf.c`` implements functionality to translate a file or buffer with a pattern written in Augmented Backus-Naur form into a finite state machine compatible with the pattern matching functionality in ``match.c``.

## Augmented BNF compiler (``augbnf.c``)

Implementation of an Augmented Backus-Naur grammar parser

#### Basic Rules:

* A semicolon `;` means the rest of the line is to be treated as a comment

* Rules are of the form
```abnf
     Rule = tokens...
```
and can be arbitrarily nested. Rule names must only be unreserved chars (see _note_ below) to avoid any ambiguity. The main defining rule of a grammar is the first rule, and any rules that are not referenced in some subtree of the first rule are ignored.


* If a rule is to take up more than one line, it must have parenthesis cross the line boundaries, otherwise the following lines will be ignored, i.e.
```abnf
     Rule0 = "a" | "b" | "c" |
             "d" | "e" | "f"
```

 would not work, but

 ```abnf
     Rule0 = ("a" | "b" | "c" |
             "d" | "e" | "f")
```

 would


* The simplest tokens are string literals, which may be a single character or multiple characters, contained in double quotes

* Token concatenation means the grammar expects one token to be immediately followed by the next, i.e. if

```abnf
     Rule1 = "a" "b" "c"
```

 then Rule1 would match only the string "abc"

* By default, a line must be fully matched by a rule, meaning, for example,

```abnf
     Rule3 = "a"
```

 would not match "ac"
 
#### Branching (``|``) and Character Classes:

* If two tokens are separated by a "|", then either may be taken, with precedence on the first, i.e.

```abnf
     Rule2 = "a" | "c" | "ca"
```

 would match only "a", "c", and "ca". If, however, the rule was
 
```abnf
     Rule2 = ("a" | "c" | "ca") ["c"]
```

 it would match the ``"a"`` in the or-ed group followed by the optional ``"c"``, and not the ``"ac"`` in the or-ed group, as the ``"a"`` matched first and successfully matched the rest of the string.

* Concatenation and branching (``|``) cannot be mixed, i.e.

```abnf
     Rule7 = "a" "b" | "c"
```

 is not allowed, as it is unclear what the ``|`` should be branching between. To do this, you would need to use parenthesis to group either the first two or last two tokens

* Classes of characters are defined as a set of allowable characters, which are written as all of the characters in the character class between angle brackets (``<>``). For example, you could match a group of characters by writing

```abnf
     digit = '0' | '1' | '2' | '3' | '4' | '5' | '6' | '7' | '8' | '9'
```

 but, using character classes, more concisely

```abnf
     digit = <0123456789>
```

 which is equivalent to the above definition. Characters in the triangle brackets, just as in string literals, can be escaped by using a backslash, i.e.

```abnf
     whitespace = < \n\t>
```

 would match a space ``' '``, a newline character ``'\n'``, or a tab ``'\t'``

#### Quantifiers:

* A ``<m>*<n>`` before a token indicates how many of the token to expect, with m being the minimum number of occurences and n being the max. Omitting m means there is no minimum (even 0 is allowed), and omitting n means there is no maximum. For example,

```abnf
     Rule4 = *Rule3
```

 would match "", "a", and "aaaaaa", but not "aaabaa"

* Putting square brackets ``[]`` around a token is shorthand for putting ``*1`` before it, i.e. it means the token is optional (expect either 0 or 1 occurences of it)

#### Capturing:

* Putting curly braces ``{}`` around a token or group of tokens indicates that it is a capturing group, meaning if it is chosen as part of the rule matching an expression, it will be put in a list of captured groups when parsed, i.e.

```abnf
     Rule5 = "a" {"b"} "c"
```

 would match "abc", and "b" would be the only captured group returned. The order in which the capturing groups are declared in the file they are written in indicates their indices in the list of matches, and thus a single capturing rule that is used in multiple places can capture at most one substring

#### Grouping:

* Parenthesis can be used to group tokens without needing to create separate rules for them, i.e.

```abnf
     Rule6 = "a" ("b" | "c")
```

 would match ``"ab"`` and ``"ac"``, but without parenthesis, you would need a separate rule for the ``("b" | "c")``, otherwise the | would be ambiguous

 

_Note:_ Unreserved characters include the following:

* alpha (``'a' - 'z'`` and ``'A' - 'Z'``)

* numeric (``'0' - '9'``)

* ``'-'``

* ``'_'``

* ``'.'``

* ``'!'``

* ``'~'``

* ``'@'``



## Pattern Matching

Patterns are implemented as a finite state machine made up of tokens and patterns. Tokens define ways of moving throughout the machine, and patterns give constrains on when actions are allowed to be made on a token.

#### Patterns can be one of three types:

* Literal: just a string, i.e. "Hello"

* Character Class: a set of characters with ASCII codes between 1-127

* Token: points to a token, which is expected to lead back to this token on all paths that do not fail


#### Tokens contain the following information pertinent to pattern matching:

* ``node``: the pattern associated with this token

* ``min``, ``max``: the minimum and maximum number of times this token may be consumed in matching before moving on to the following token (``next``)

* ``next``: the subsequent token to be processed after sucessful consumption of this token

* ``alt``: an alternative token to consume in place of this token if successful completion of pattern matching is found to not be possible following this token


#### Consumption rules:

* A literal can be consumed on an input buffer (string) if the string exactly matches the beginning of the buffer, and upon consumption the literal advances the buffer to the end of the matched string

* A character class can be consumed on an input buffer if the first character in the buffer is in the character class, and upon consumption the character class advances the buffer one location

* A token is consumed by consuming whatever its ``node`` points to on the current state of the input buffer


#### FSM formation rules:

1. A pattern is represented by an FSM consisting of tokens, literals and character classes, which are all linked together by tokens.

2. No token may be referenced by a token which lies ahead of it along any path of ``next``'s and ``alt``'s, i.e. the graph must be acyclic along edges represented by the ``next`` and ``alt`` fields of the tokens

3. Tokens which contain other tokens (i.e. a token whose ``node`` is also a token) require that that child token lead back to the parent on all matching paths (following any of ``next``, ``alt``, and ``node`` fields of tokens), and that no paths lead to termination (i.e. ``next = NULL``). Also, no token in the subgraph within the token may be a parent of the token (for example, ``node`` could not point to the token whose ``next`` is token, even if that would not cause any cycles or violate rule 2)

4. If a token is an ``alt`` of some other token, then it cannot be referenced by any other token (as a ``next``, ``alt``, or ``node``), i.e. its reference count must be exactly 1

5. No token may have a ``node`` value of NULL


#### FSM iteration rules:

1. A token may follow its ``next`` pointer if and only if it has been adjacently consumed at least "``min``" number of times and at most "``max``" times

2. A token may follow its ``alt`` pointer if and only if it has not been consumed

3. A match is found on an input buffer if some path through the FSM entirely consumes the buffer and ends on a ``NULL`` ``next`` node, i.e. the last token to consume the buffer to completion has a ``next`` value of ``NULL``



## FSM Optimization

A generic pattern-matching FSM abiding by all of the above rules can be consolidated with ``pattern_consolidate``, which tries
to make the FSM as small and compact as possible, by

* Removing unecessary encapsulation (like a once-required token (i.e. 1\*1) with a ``node`` of type token)
* Merging multiple adjacent strings into a large string
* Merging multiple or-ed single-characters/character classes into one character class


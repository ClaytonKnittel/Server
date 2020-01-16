# Augmented Backus-Naur Form Pattern Matching

An implementation of generic pattern matching and an Augmented BNF compiler. The pattern matching is implemented as a finite state machine in ``match.c``, fully capable of capturing tokens and recursive backtracking. ``augbnf.c`` implements functionality to translate a file or buffer with a pattern written in Augmented Backus-Naur form into a finite state machine compatible with the pattern matching functionality in ``match.c``.

## Augmented BNF compiler (``augbnf.c``)

Implementation of an Augmented Backus-Naur grammar parser

Rules:

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

* If two tokens are separated by a "|", then either may be taken, with precedence on the first, i.e.

```abnf
     Rule2 = "a" | "c" | "ca"
```

 would match only "a", "c", and "ca". If, however, the rule was
 
```abnf
     Rule2 = ("a" | "c" | "ca") ["c"]
```

 it would match the ``"a"`` in the or-ed group followed by the optional ``"c"``, and not the ``"ac"`` in the or-ed group, as the ``"a"`` matched first and successfully matched the rest of the string.

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

* By default, a line must be fully matched by a rule, meaning, for example,

```abnf
     Rule3 = "a"
```

 would not match "ac"

* A ``<m>*<n>`` before a token indicates how many of the token to expect, with m being the minimum number of occurences and n being the max. Omitting m means there is no minimum (even 0 is allowed), and omitting n means there is no maximum. For example,

```abnf
     Rule4 = *Rule3
```

 would match "", "a", and "aaaaaa", but not "aaabaa"

* Putting square brackets ``[]`` around a token is shorthand for putting ``*1`` before it, i.e. it means the token is optional (expect either 0 or 1 occurences of it)

* Putting curly braces ``{}`` around a token or group of tokens indicates that it is a capturing group, meaning if it is chosen as part of the rule matching an expression, it will be put in a list of captured groups when parsed, i.e.

```abnf
     Rule5 = "a" {"b"} "c"
```

 would match "abc", and "b" would be the only captured group returned. The order in which the capturing groups are declared in the file they are written in indicates their indices in the list of matches, and thus a single capturing rule that is used in multiple places can capture at most one substring

* Parenthesis can be used to group tokens without needing to create separate rules for them, i.e.

```abnf
     Rule6 = "a" ("b" | "c")
```

 would match ``"ab"`` and ``"ac"``, but without parenthesis, you would need a separate rule for the ``("b" | "c")``, otherwise the | would be ambiguous

* Concatenation and branching (``|``) cannot be mixed, i.e.

```abnf
     Rule7 = "a" "b" | "c"
```

 is not allowed, as it is unclear what the ``|`` should be branching between. To do this, you would need to use parenthesis to group either the first two or last two tokens

 

_Note:_ Unreserved characters include the following:

* alpha (``'a' - 'z'`` and ``'A' - 'Z'``)

* numeric (``'0' - '9'``)

* ``'-'``

* ``'_'``

* ``'.'``

* ``'!'``

* ``'~'``

* ``'@'``


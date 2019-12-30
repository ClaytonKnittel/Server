

/*
 * implementation of a grammar parser inspired by Augmented Backus-Naur form
 * 
 * Rules:
 *
 *  A semicolon ';' means the rest of the line is to be treated as a comment
 *
 *  Rules are of the form
 *      Rule = tokens...
 *  and can be arbitrarily nested. However, a rule's definition must appear
 *  before it is used as a token anywhere else. Rule names must only be alpha
 *  characters to avoid any ambiguity
 *
 *
 *  The simplest tokens are string literals, which may be a single character or
 *  multiple characters, contained in double quotes
 *
 *
 *  Token concatenation means the grammar expects one token to be immediately
 *  followed by the next, i.e. if
 *
 *      Rule1 = "a" "b" "c"
 *
 *  then Rule1 would match only the string "abc"
 *
 *
 *  If two tokens are separated by a "|", then either may be taken, with
 *  precedence on the first, i.e.
 *      
 *      Rule2 = "a" | "c" | "ca"
 *
 *  would match "a", "c", and "ca"
 *
 *
 *  By default, a line must be fully matched by a rule, meaning, for example,
 *
 *      Rule3 = "a"
 *
 *  would not match "ac"
 *
 *
 *  A <m>*<n> before a token indicates how many of the token to expect, with
 *  m being the minimum number of occurences and n being the max. Omitting m
 *  means there is no minimum (even 0 is allowed), and omitting n means there
 *  is no maximum. For example,
 *
 *      Rule4 = *Rule3
 *
 *  would match "", "a", and "aaaaaa", but not "aaabaa"
 *
 *
 *  Putting square brackets [] around a token is shorthand for putting *1
 *  before it, i.e. it means the token is optional (expect either 0 or 1
 *  occurences of it)
 *
 *
 *  Putting curly braces {} around a token or group of tokens indicates that it
 *  is a capturing group, meaning if it is chosen as part of the rule matching
 *  an expression, it will be put in a list of captured groups when parsed,
 *  i.e.
 *
 *      Rule5 = "a" {"b"} "c"
 *
 *  would match "abc", and "b" would be the only captured group returned
 *
 *
 *  Parenthesis can be used to group tokens without needing to create separate
 *  rules for them, i.e.
 *
 *      Rule6 = "a" ("b" | "c")
 *
 *  would match "ab" and "ac", but without parenthesis, you would need a
 *  separate rule for the ("b" | "c"), otherwise the | would be ambiguous
 *
 *
 *  Concatenation and branching (|) cannot be mixed, i.e.
 *
 *      Rule7 = "a" "b" | "c"
 *
 *  is not allowed, as it is unclear what the | should be branching between.
 *  To do this, you would need to use parenthesis to group either the first
 *  two or last two tokens
 *
 *
 *
 *
 *
 */



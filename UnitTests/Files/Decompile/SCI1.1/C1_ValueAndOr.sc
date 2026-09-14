;;; Sierra Script 1.0 - (do not remove this comment)
(script# 908)
(include sci.sh)

; A logical and/or used for its value, not as a condition. Compiled by SCI
; Companion's own compiler. After the value-and/or compiler fix, these leave
; the last evaluated operand in the accumulator (Sierra semantics), so the
; bytecode round-trips. Three operands, a not, an argument, and an and below
; a compare inside an if test (the operands of a compare are values, so the
; and must not branch to the if's else).
(public
	c1 0
)

(procedure (c1 a b c &tmp t)
	(= t (or a b))
	(= t (and a b))
	(= t (or a (and b c)))
	(= t (and a (or b c)))
	(= t (and a b c))
	(= t (not (and a b)))
	(= t (c1 (or a b) 1 2))
	(if (== (and a b) 5)
		(= t 1)
	)
	(return (and a b))
)

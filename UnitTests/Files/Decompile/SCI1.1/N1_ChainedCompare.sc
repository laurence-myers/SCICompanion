;;; Sierra Script 1.0 - (do not remove this comment)
(script# 923)
(include sci.sh)

; Chained comparisons. Sierra compiles (< 0 x 19) with a pprev so the middle
; is evaluated once; the decompiler builds the n-ary comparison back from the
; pprev at instruction consumption. A comparison whose operands are not
; shared stays an and. A send in the middle is evaluated once, as a chain of
; four operands, a loop test, and an or operand all fold too.
(public
	n1ChainedCompare 0
)

(procedure (n1ChainedCompare param1 &tmp temp0)
	(= temp0 0)
	(if (< 0 param1 19)
		(= temp0 1)
	)
	(if (<= 1 param1 15)
		(++ temp0)
	)
	(if (and (< param1 5) (> temp0 10))
		(= temp0 2)
	)
	(if (<= 1 (param1 x?) 15)
		(= temp0 3)
	)
	(if (== 0 param1 temp0 5)
		(= temp0 4)
	)
	(while (< 0 temp0 19)
		(-- temp0)
	)
	(if (or (== temp0 7) (< 1 temp0 3))
		(= temp0 5)
	)
	(return (< 5 param1 10))
)

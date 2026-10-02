;;; Sierra Script 1.0 - (do not remove this comment)
(script# 954)
(include sci.sh)

(public
	l1BreakTwoLevels 0
	l1ContinueInDo 1
)

(procedure (l1BreakTwoLevels param1 &tmp temp0 temp1)
	(while (< temp0 param1)
		(= temp1 0)
		(while (< temp1 param1)
			(if (== temp1 7) (break 2))
			(++ temp1)
		)
		(++ temp0)
	)
)

(procedure (l1ContinueInDo &tmp temp0 temp1 temp2)
	(repeat
		(if temp0 (= temp1 1) (continue))
		(breakif(not temp2))
	)
)


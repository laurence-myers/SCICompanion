;;; Sierra Script 1.0 - (do not remove this comment)
(script# 932)
(include sci.sh)

(public
	c3SierraIndexedMathAssign 0
)

(procedure (c3SierraIndexedMathAssign param1 param2 &tmp [temp0 4])
	(+= [temp0 param1] param2)
	(-= [temp0 (+ param1 1)] 1)
)

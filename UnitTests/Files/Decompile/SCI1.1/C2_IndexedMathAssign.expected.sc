;;; Sierra Script 1.0 - (do not remove this comment)
(script# 920)
(include sci.sh)

(public
	c2IndexedMathAssign 0
)

(procedure (c2IndexedMathAssign param1 param2 &tmp [temp0 4])
	(+= [temp0 param1] param2)
	(-= [temp0 2] 1)
	(return [temp0 param1])
)

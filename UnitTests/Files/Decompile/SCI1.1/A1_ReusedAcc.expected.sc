;;; Sierra Script 1.0 - (do not remove this comment)
(script# 928)
(include sci.sh)
(use System)


(class A1Reuse of Code
	(properties
		x 0
	)
	
	(method (doit theX &tmp theX_2 temp1)
		(= theX_2 5)
		(theX_2 init:)
		(= theX_2 theX)
		(temp1 perform: theX_2)
		(= x theX)
		(temp1 perform: x)
		(= theX_2 theX)
		((= x theX_2) init: self &rest)
	)
)

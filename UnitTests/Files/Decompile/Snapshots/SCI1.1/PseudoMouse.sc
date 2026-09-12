;;; Sierra Script 1.0 - (do not remove this comment)
(script# 933)
(include sci.sh)
(use Main)
(use System)


(class PseudoMouse of Code
	(properties
		cursorInc 2
		minInc 2
		maxInc 20
		prevDir 0
		joyInc 5
	)
	
	(method (doit &tmp gPEventX_2 gPEventY_2)
		(= gPEventX_2 (gPEvent x?))
		(= gPEventY_2 (gPEvent y?))
		(switch prevDir
			(1 (-= gPEventY_2 cursorInc))
			(2
				(+= gPEventX_2 cursorInc)
				(-= gPEventY_2 cursorInc)
			)
			(3 (+= gPEventX_2 cursorInc))
			(4
				(+= gPEventX_2 cursorInc)
				(+= gPEventY_2 cursorInc)
			)
			(5 (+= gPEventY_2 cursorInc))
			(6
				(-= gPEventX_2 cursorInc)
				(+= gPEventY_2 cursorInc)
			)
			(7 (-= gPEventX_2 cursorInc))
			(8
				(-= gPEventX_2 cursorInc)
				(-= gPEventY_2 cursorInc)
			)
		)
		(gGame setCursor: gCursorNumber 1 gPEventX_2 gPEventY_2)
	)
	
	(method (handleEvent param1 &tmp temp0 thePrevDir temp2)
		(= temp0 (param1 type?))
		(= thePrevDir (param1 message?))
		(= temp2 (param1 modifiers?))
		(if (& temp0 $0040)
			(= prevDir thePrevDir)
			(= cursorInc
				(if (& temp0 $0004)
					(if (& temp2 $0003) minInc else maxInc)
				else
					joyInc
				)
			)
			(cond 
				((& temp0 $0004)
					(if prevDir
						(self doit:)
					else
						(param1 claimed: 0)
						(return)
					)
				)
				(prevDir (self start:))
				(else (self stop:))
			)
			(param1 claimed: 1)
			(return)
		)
	)
	
	(method (start thePrevDir)
		(if argc (= prevDir thePrevDir))
		(gTheDoits add: self)
	)
	
	(method (stop)
		(= prevDir 0)
		(gTheDoits delete: self)
	)
)

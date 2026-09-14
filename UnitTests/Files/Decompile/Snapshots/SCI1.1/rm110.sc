;;; Sierra Script 1.0 - (do not remove this comment)
(script# 110)
(include sci.sh)
(use Main)
(use Game)

(public
	rm110 0
)

(local
	[P_Default110 11] = [1 3 4 319 189 319 50 0 50 0 189]
)
(instance rm110 of Rm
	(properties
		noun 2
		picture 110
		style -32758
		horizon 50
		vanishingX 130
		vanishingY 50
	)
	
	(method (init)
		(AddPolygonsToRoom @P_Default110)
		(super init:)
		(switch gPreviousRoomNumber
			(else 
				(SetUpEgo -1 1)
				(gEgo posn: 150 130)
			)
		)
		(gEgo init:)
		(gGame handsOn:)
	)
)

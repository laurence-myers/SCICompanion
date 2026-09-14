;;; Sierra Script 1.0 - (do not remove this comment)
(script# 100)
(include sci.sh)
(use Main)
(use Controls)
(use Print)
(use Game)
(use System)

(public
	rm100 0
)

(instance myDialog of Dialog
	(properties)
)

(instance rm100 of Rm
	(properties
		picture 100
	)
	
	(method (init)
		(SetPort 0 0 200 320 0 0)
		(if gDialog (gDialog dispose:))
		(super init:)
		(gOldMH addToFront: self)
		(gOldKH addToFront: self)
		(gGame setCursor: 996 1)
		(gIconBar hide: disable:)
		(gUser canInput: 1)
		(self setScript: rmScript)
	)
	
	(method (dispose)
		(SetPort 0 0 190 320 10 0)
		(gIconBar hide: enable:)
		(= gNormalCursor 999)
		(gGame setCursor: 996 1)
		(gOldKH delete: self)
		(gOldMH delete: self)
		(super dispose: &rest)
	)
	
	(method (handleEvent param1)
		(if
			(and
				(!= (rmScript state?) 1)
				(& (param1 type?) (| $4000 $0001 $0002 $0004))
			)
			(rmScript changeState: 1)
			(param1 claimed: 1)
			(return)
		else
			(super handleEvent: param1)
		)
	)
)

(instance rmScript of Script
	(properties)
	
	(method (changeState theState &tmp temp0 [temp1 10])
		(switch (= state theState)
			(0 (= seconds 4))
			(1
				(= seconds 0)
				(= gNormalCursor 999)
				(gGame setCursor: 999 1)
				(Print
					dialog: myDialog
					font: gFont
					width: 150
					mode: 1
					addText: 8 1 0 4 0 0 0
					addText: 8 1 0 5 0 10 0
					addColorButton: 0 8 1 0 1 0 20 0 0 11 23 5 5 5
					addColorButton: 1 8 1 0 2 0 30 0 0 11 23 5 5 5
				)
				(= temp0
					(Print
						addColorButton: 2 8 1 0 3 0 40 0 0 11 23 5 5 5
						init:
					)
				)
				(switch temp0
					(0 (gRoom newRoom: 110))
					(1
						(gGame restore:)
						(self changeState: state)
					)
					(2 (= gQuitGame 1))
				)
			)
		)
	)
)

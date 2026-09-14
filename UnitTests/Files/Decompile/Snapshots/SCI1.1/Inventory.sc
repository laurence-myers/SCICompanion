;;; Sierra Script 1.0 - (do not remove this comment)
(script# 15)
(include sci.sh)
(use Main)
(use ScrollableInventory)
(use ScrollInsetWindow)
(use IconItem)
(use System)

(public
	invCode 0
	invWin 1
)

(instance templateInventory of ScrollableInventory
	(properties)
	
	(method (init)
		(super init: &rest)
		(invWin
			color: gColorWindowForeground
			back: 3
			topBordColor: 5
			lftBordColor: 4
			rgtBordColor: 2
			botBordColor: 1
			insideColor: 2
			topBordColor2: 1
			lftBordColor2: 1
			botBordColor2: 6
			rgtBordColor2: 6
		)
		(self
			eachElementDo: 220 2
			add: invLook invSelect invHelp invUp invDown ok
		)
		(self
			state: 2048
			upIcon: invUp
			downIcon: invDown
			window: invWin
			helpIconItem: invHelp
			selectIcon: invSelect
			okButton: ok
			numCols: 5
			scrollAmount: 5
			dispAmount: 10
			empty: 13
			normalHeading: 15
			eachElementDo: 219 0
			eachElementDo: 222 15
			eachElementDo: 110
		)
	)
)

(instance invCode of Code
	(properties)
	
	(method (init)
		(= gInv templateInventory)
		(gInv init:)
	)
)

(instance invWin of ScrollInsetWindow
	(properties
		priority -1
		topBordHgt 30
		botBordHgt 5
	)
	
	(method (open)
		(invLook
			nsLeft: (- (/ (- (self right?) (self left?)) 2) 84)
		)
		(invLook nsTop: 2)
		(super open: &rest)
	)
)

(instance invUp of IconItem
	(properties
		view 991
		loop 5
		cel 0
		cursor 999
		maskView 991
		maskLoop 5
		maskCel 2
		lowlightColor 5
		noun 18
		helpVerb 5
	)
	
	(method (select)
		(if (super select: &rest) (gInv scroll: -1))
		(return 0)
	)
)

(instance invDown of IconItem
	(properties
		view 991
		loop 6
		cel 0
		cursor 999
		maskView 991
		maskLoop 6
		maskCel 2
		lowlightColor 5
		noun 11
		helpVerb 5
	)
	
	(method (select)
		(if (super select: &rest) (gInv scroll: 1))
		(return 0)
	)
)

(instance ok of IconItem
	(properties
		view 991
		loop 3
		cel 0
		cursor 999
		signal 67
		lowlightColor 5
		noun 16
		helpVerb 5
	)
)

(instance invLook of IconItem
	(properties
		view 991
		loop 2
		cel 0
		cursor 981
		message 1
		signal 129
		lowlightColor 5
		noun 15
		helpVerb 5
	)
)

(instance invHand of IconItem
	(properties
		view 991
		loop 0
		cel 0
		cursor 982
		message 4
		lowlightColor 5
		noun 12
		helpVerb 5
	)
)

(instance invHelp of IconItem
	(properties
		view 991
		loop 1
		cel 0
		cursor 989
		message 5
		lowlightColor 5
		noun 14
		helpVerb 5
	)
)

(instance invSelect of IconItem
	(properties
		view 991
		loop 4
		cel 0
		cursor 999
		lowlightColor 5
		noun 17
		helpVerb 5
	)
)

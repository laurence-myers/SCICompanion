;;; Sierra Script 1.0 - (do not remove this comment)
(script# 987)
(include sci.sh)
(use Main)
(use Controls)
(use DialogControls)
(use SysWindow)


(local
	newDText_2
	newDButton_2
	newDButton
	newDText
	newDButton_3
	newDButton_4
	newDButton_5
	[local7 100]
)
(class Gauge of Dialog
	(properties
		elements 0
		size 0
		text 0
		font 0
		window 0
		theItem 0
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		time 0
		caller 0
		seconds 0
		lastSeconds 0
		eatTheMice 0
		lastTicks 0
		description 0
		higher {up}
		lower {down}
		normal 7
		minimum 0
		maximum 15
	)
	
	(method (init param1 &tmp temp0 temp1)
		(= window SysWindow)
		(self update: param1)
		(= newDButton (DButton new:))
		(newDButton text: lower moveTo: 4 4 setSize:)
		(self add: newDButton setSize:)
		(= newDText (DText new:))
		(newDText
			text: @local7
			moveTo: (+ (newDButton nsRight?) 4) 4
			font: 0
			setSize:
		)
		(self add: newDText setSize:)
		(= newDButton_2 (DButton new:))
		(newDButton_2
			text: higher
			moveTo: (+ (newDText nsRight?) 4) 4
			setSize:
		)
		(self add: newDButton_2 setSize:)
		(+= nsBottom 8)
		(= newDButton_3 (DButton new:))
		(newDButton_3 text: {OK} setSize: moveTo: 4 nsBottom)
		(= newDButton_4 (DButton new:))
		(newDButton_4
			text: {Normal}
			setSize:
			moveTo: (+ (newDButton_3 nsRight?) 4) nsBottom
		)
		(= newDButton_5 (DButton new:))
		(newDButton_5
			text: {Cancel}
			setSize:
			moveTo: (+ (newDButton_4 nsRight?) 4) nsBottom
		)
		(self
			add: newDButton_3 newDButton_4 newDButton_5
			setSize:
		)
		(= temp0 (- (- nsRight (newDButton_5 nsRight?)) 4))
		(= newDText_2 (DText new:))
		(newDText_2
			text: description
			font: gSmallFont
			setSize: (- nsRight 8)
			moveTo: 4 4
		)
		(= temp1 (+ (newDText_2 nsBottom?) 4))
		(self add: newDText_2)
		(newDButton_2 move: 0 temp1)
		(newDButton move: 0 temp1)
		(newDText move: 0 temp1)
		(newDButton_3 move: temp0 temp1)
		(newDButton_4 move: temp0 temp1)
		(newDButton_5 move: temp0 temp1)
		(self setSize: center: open: 4 15)
	)
	
	(method (doit theTheNormal &tmp temp0 theNormal)
		(self init: theTheNormal)
		(= theNormal theTheNormal)
		(repeat
			(self update: theNormal)
			(newDText draw:)
			(cond 
				(
				(== (= temp0 (super doit: newDButton_3)) newDButton_2) (if (< theNormal maximum) (++ theNormal)))
				((== temp0 newDButton) (if (> theNormal minimum) (-- theNormal)))
				((== temp0 newDButton_3) (break))
				(else
					(if (== temp0 newDButton_4)
						(= theNormal normal)
						(continue)
					)
					(if (or (== temp0 0) (== temp0 newDButton_5))
						(= theNormal theTheNormal)
						(break)
					)
				)
			)
		)
		(self dispose:)
		(return theNormal)
	)
	
	(method (handleEvent param1)
		(switch (param1 type?)
			(4
				(switch (param1 message?)
					(19200
						(param1 claimed: 1)
						(return newDButton)
					)
					(19712
						(param1 claimed: 1)
						(return newDButton_2)
					)
				)
			)
			(64
				(switch (param1 message?)
					(7
						(param1 claimed: 1)
						(return newDButton)
					)
					(3
						(param1 claimed: 1)
						(return newDButton_2)
					)
				)
			)
		)
		(super handleEvent: param1)
	)
	
	(method (update param1 &tmp temp0 temp1)
		(= temp1 (- maximum minimum))
		(= temp0 0)
		(while (< temp0 temp1)
			(StrAt @local7 temp0 (if (< temp0 param1) 6 else 7))
			(++ temp0)
		)
	)
)

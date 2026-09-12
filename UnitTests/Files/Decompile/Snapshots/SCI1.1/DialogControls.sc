;;; Sierra Script 1.0 - (do not remove this comment)
(script# 922)
(include sci.sh)
(use Main)
(use Controls)
(use System)


(class DIcon of Control
	(properties
		type 4
		state 0
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		key 0
		said 0
		value 0
		view 0
		loop 0
		cel 0
	)
	
	(method (setSize)
		(= nsRight (+ nsLeft (CelWide view loop cel)))
		(= nsBottom (+ nsTop (CelHigh view loop cel)))
	)
)

(class DButton of Control
	(properties
		type 1
		state 3
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		key 0
		said 0
		value 0
		text 0
		font 0
	)
	
	(method (dispose param1)
		(if (and text (or (not argc) (not param1)))
			(Memory 3 (self text?))
		)
		(super dispose:)
	)
	
	(method (setSize &tmp [temp0 2] temp2 temp3)
		(TextSize @temp0 text font 0 0)
		(+= temp2 2)
		(+= temp3 2)
		(= nsBottom (+ nsTop temp2))
		(= temp3 (* (/ (+ temp3 15) 16) 16))
		(= nsRight (+ temp3 nsLeft))
	)
)

(class DEdit of Control
	(properties
		type 3
		state 1
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		key 0
		said 0
		value 0
		text 0
		font 0
		max 0
		cursor 0
	)
	
	(method (track param1)
		(EditControl self param1)
		(return self)
	)
	
	(method (setSize &tmp [temp0 2] temp2 temp3)
		(= font gInputFont)
		(TextSize @temp0 {M} font 0 0)
		(= nsBottom (+ nsTop temp2))
		(= nsRight (+ nsLeft (/ (* temp3 max 3) 4)))
		(= cursor (StrLen text))
	)
)

(class DSelector of Control
	(properties
		type 6
		state 0
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		key 0
		said 0
		value 0
		font 0
		x 20
		y 6
		text 0
		cursor 0
		topString 0
		mark 0
	)
	
	(method (handleEvent param1 &tmp temp0 [temp1 3] temp4 [temp5 2] temp7 temp8)
		(if (param1 claimed?) (return 0))
		(= temp0 0)
		(switch (param1 type?)
			(4
				(param1
					claimed:
						(switch (param1 message?)
							(18176 (self retreat: 50))
							(20224 (self advance: 50))
							(20736 (self advance: (- y 1)))
							(18688 (self retreat: (- y 1)))
							(20480 (self advance: 1))
							(18432 (self retreat: 1))
							(else  0)
						)
				)
			)
			(1
				(if (self check: param1)
					(param1 claimed: 1)
					(cond 
						((< (param1 y?) (+ nsTop 10))
							(repeat
								(self retreat: 1)
								(breakif(not (MouseStillDown)))
							)
						)
						((> (param1 y?) (- nsBottom 10))
							(repeat
								(self advance: 1)
								(breakif(not (MouseStillDown)))
							)
						)
						(else
							(TextSize @temp5 {M} font 0 0)
							(= temp4 (/ (- (param1 y?) (+ nsTop 10)) temp7))
							(if (> temp4 mark)
								(self advance: (- temp4 mark))
							else
								(self retreat: (- mark temp4))
							)
						)
					)
				)
			)
		)
		(return (and (param1 claimed?) (& state $0002) self))
	)
	
	(method (setSize &tmp [temp0 2] temp2 temp3)
		(TextSize @temp0 {M} font 0 0)
		(= nsBottom (+ nsTop 20 (* temp2 y)))
		(= nsRight (+ nsLeft (/ (* temp3 x 3) 4)))
		(= topString (= cursor text))
		(= mark 0)
	)
	
	(method (indexOf param1 &tmp theText temp1)
		(= theText text)
		(= temp1 0)
		(return
			(while (< temp1 300)
				(if (== 0 (StrLen theText)) (return -1))
				(if (not (StrCmp param1 theText)) (return temp1))
				(+= theText x)
				(++ temp1)
			)
		)
	)
	
	(method (at param1)
		(return (+ text (* x param1)))
	)
	
	(method (advance param1 &tmp temp0)
		(if (not (StrAt cursor 0))
			(return (not (StrAt cursor 0)))
		)
		(= temp0 0)
		(while (and param1 (StrAt cursor x))
			(= temp0 1)
			(+= cursor x)
			(if (< (+ mark 1) y) (++ mark) else (+= topString x))
			(-- param1)
		)
		(return (if temp0 (self draw:) 1 else 0))
	)
	
	(method (retreat param1 &tmp temp0)
		(= temp0 0)
		(while (and param1 (!= cursor text))
			(= temp0 1)
			(-= cursor x)
			(if mark (-- mark) else (-= topString x))
			(-- param1)
		)
		(return (if temp0 (self draw:) 1 else 0))
	)
)

(class Controls of List
	(properties
		elements 0
		size 0
	)
	
	(method (draw)
		(self eachElementDo: 186)
		(self eachElementDo: 80)
	)
	
	(method (handleEvent param1 &tmp temp0)
		(if (param1 claimed?) (return 0))
		(= temp0 (self firstTrue: 133 param1))
		(if
			(and
				temp0
				(not
					((= temp0 (self firstTrue: 133 param1)) checkState: 2)
				)
			)
			(temp0 doit:)
			(= temp0 0)
		)
		(return temp0)
	)
)

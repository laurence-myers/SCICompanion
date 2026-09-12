;;; Sierra Script 1.0 - (do not remove this comment)
(script# 934)
(include sci.sh)
(use IconItem)
(use System)


(class Slider of IconItem
	(properties
		view -1
		loop -1
		cel -1
		nsLeft 0
		nsTop -1
		nsRight 0
		nsBottom 0
		state 0
		cursor -1
		type 16384
		message -1
		modifiers 0
		signal 1
		maskView 0
		maskLoop 0
		maskCel 0
		highlightColor 0
		lowlightColor 0
		noun 0
		modNum 0
		helpVerb 0
		sliderView 0
		sliderLoop 0
		sliderCel 0
		sTop 0
		sLeft 0
		sRight 0
		maxY 0
		minY 0
		underBits 0
		yStep 1
		theObj 0
		selector 0
		bottomValue 0
		topValue 0
	)
	
	(method (doit)
		(if theObj (Eval theObj selector &rest))
	)
	
	(method (show)
		(super show: &rest)
		(if (not sRight)
			(= sLeft nsLeft)
			(= sRight nsRight)
			(= maxY
				(- nsBottom (CelHigh sliderView sliderLoop sliderCel))
			)
			(= minY nsTop)
		)
		(= sTop (self valueToPosn:))
		(DrawCel sliderView sliderLoop sliderCel sLeft sTop -1)
		(Graph
			12
			(- nsTop 1)
			(- nsLeft 1)
			(+ 2 nsBottom)
			(+ 2 nsRight)
			1
		)
	)
	
	(method (select param1 &tmp newEvent)
		(return
			(if (and argc param1)
				(while (!= ((= newEvent (Event new:)) type?) 2)
					(newEvent localize:)
					(cond 
						((< (newEvent y?) (- sTop yStep)) (self move: yStep (not (& signal $0200))))
						((> (newEvent y?) (+ sTop yStep)) (self move: (- yStep) (not (& signal $0200))))
					)
					(newEvent dispose:)
				)
				(if (& signal $0200)
					(self doit: (self posnToValue: sTop))
				)
				(newEvent dispose:)
			else
				(return 1)
			)
		)
	)
	
	(method (highlight)
	)
	
	(method (advance)
		(self
			move:
				(Max
					yStep
					(-
						sTop
						(self
							valueToPosn: (+ (self doit:) (Sign (- topValue bottomValue)))
						)
					)
				)
				(not (& signal $0200))
		)
		(if (& signal $0200)
			(self doit: (self posnToValue: sTop))
		)
	)
	
	(method (retreat)
		(self
			move:
				(Min
					(- yStep)
					(-
						sTop
						(self
							valueToPosn: (- (self doit:) (Sign (- topValue bottomValue)))
						)
					)
				)
				(not (& signal $0200))
		)
		(if (& signal $0200)
			(self doit: (self posnToValue: sTop))
		)
	)
	
	(method (move param1 param2 &tmp temp0 temp1 temp2 temp3 temp4 temp5 temp6 temp7)
		(= temp7 (if (not argc) else param2))
		(= temp5 (Sign param1))
		(= temp4 param1)
		(while (<= yStep (Abs temp4))
			(= temp0 (- sTop (* temp5 yStep)))
			(= temp1 (CelHigh sliderView sliderLoop sliderCel))
			(= sTop
				(cond 
					((< temp0 minY) minY)
					((> temp0 maxY) maxY)
					(else temp0)
				)
			)
			(= temp2 (PicNotValid))
			(PicNotValid 1)
			(DrawCel view loop cel nsLeft nsTop -1)
			(DrawCel sliderView sliderLoop sliderCel sLeft sTop -1)
			(Graph
				12
				(- nsTop 1)
				(- nsLeft 1)
				(+ 2 nsBottom)
				(+ 2 nsRight)
				1
			)
			(PicNotValid temp2)
			(= temp3 (self posnToValue: sTop))
			(= temp6
				(if temp7 (self doit: temp3) else (self doit:))
			)
			(-= temp4 (* yStep temp5))
		)
		(return temp6)
	)
	
	(method (valueToPosn param1 &tmp temp0)
		(= temp0 (if argc param1 else (self doit:)))
		(return
			(cond 
				((and (< temp0 topValue) (< temp0 bottomValue)) (if (< bottomValue topValue) maxY else minY))
				((and (> temp0 topValue) (> temp0 bottomValue)) (if (< bottomValue topValue) minY else maxY))
				(else
					(+
						minY
						(/
							(* (Abs (- topValue temp0)) (- maxY minY))
							(Abs (- topValue bottomValue))
						)
					)
				)
			)
		)
	)
	
	(method (posnToValue param1)
		(return
			(+
				bottomValue
				(/
					(* (- maxY param1) (- topValue bottomValue))
					(- maxY minY)
				)
			)
		)
	)
)

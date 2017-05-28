# Light-Blending-Playground

A project for testing different ways of blending light. Current blends are...

Additive:<br />
OutColor = InColorA + InColorB + ...;

Lighten:<br />
OutColor = Color( max(InColorA.r, InColorB.r, ...), max(InColorA.g, InColorB.g, ...), max(InColorA.b, InColorB.b, ...) );

Lighten Blend:<br />
for each light<br />
	OutColor = lerp( OutColor, InColor[i], att*(1/(MaxAtt*2)) );<br />
	MaxAtt = max(att, MaxAtt);<br />

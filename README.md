# Light-Blending-Playground

A project for testing different ways of blending light. Current blends are...

Additive:
OutColor = InColorA + InColorB + ...;

Lighten:
OutColor = Color( max(InColorA.r, InColorB.r, ...), max(InColorA.g, InColorB.g, ...), max(InColorA.b, InColorB.b, ...) );

Lighten Blend:
for each light
	OutColor = lerp( OutColor, InColor[i], att*(1/(MaxAtt*2)) );
	MaxAtt = max(att, MaxAtt);
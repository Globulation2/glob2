/* glob to gimp pal converter, (c) 2001 Stephane Magnenat */

#include <stdio.h>

int main(int argc, char *argv[])
{
	if (argc!=3)
		printf("usage %s src dest\n",argv[0]);
	else
	{
		FILE *src=fopen(argv[1],"r");
		FILE *dest=fopen(argv[2],"w");
		int i=0;

		fprintf(dest, "GIMP Palette\n");
		
		while (!feof(src))
		{
			unsigned char r, g, b;

			fread(&r, 1, 1, src);
			fread(&g, 1, 1, src);
			fread(&b, 1, 1, src);
			if (!feof(src))
			fprintf(dest,"%d %d %d color %d\n", r, g, b, i++);
		}
		fclose(src);
		fclose(dest);
	}
}

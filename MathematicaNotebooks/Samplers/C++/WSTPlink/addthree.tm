:Begin:
:Function:       addthree
:Pattern:        AddThree[i_Integer, j_Integer, k_Integer]
:Arguments:      { i, j, k }
:ArgumentTypes:  { Integer, Integer, Integer }
:ReturnType:     Integer
:End:

:Evaluate: AddThree::usage = "AddThree[x, y, z] gives the sum of three machine integers x y and z."

int addthree( int i, int j, int k)
{
    return i+j+k;
}

int main(int argc, char* argv[])
{
    return WSMain(argc, argv);
}
